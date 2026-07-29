#pragma once

#include "gargantuan/render/InstanceData.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/render/ShaderReflection.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <lua.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <vector>

namespace gargantuan {
	class Camera;
	class ComputeShader;
	class BasePart;
	class EditableImage;
	class PostProcessShader;
	class SurfaceShader;
	class ShaderScript;
	class ThreadEngine;

	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateVelocityPass(SDL_GPUDevice *gpu);

	class RenderProvider {
	  public:
		static constexpr SDL_GPUTextureFormat OFFSCREEN_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		static constexpr SDL_GPUTextureFormat VELOCITY_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;

		static constexpr SDL_GPUTextureFormat VIEW_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

		struct CameraTarget {
			SDL_GPUTexture *ColorTexture = nullptr;
			SDL_GPUTexture *ScratchTexture = nullptr;
			// Prior frame, allocated only for cross-frame sampling.
			SDL_GPUTexture *HistoryTexture = nullptr;
			// One geometry pass writes both; allocate them together.
			SDL_GPUTexture *VelocityTexture = nullptr;
			SDL_GPUTexture *ViewDepthTexture = nullptr;
			SDL_GPUTexture *ViewDepthHistoryTexture = nullptr;
			SDL_GPUTexture *DepthTexture = nullptr;
			// Output before the first always-redraw pass; reuse it for a still scene.
			SDL_GPUTexture *CacheTexture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		// Bound in-flight frames; each adds measured ~3.3 MB. Allows N-1 older frames.
		void BeginFrame(int maximumFramesInFlight);
		void EndFrame();

		void Draw(DrawContext drawContext);
		// Whole-world hash; per-camera checks narrow changes without fragile dirty flags.
		uint64_t ComputeSceneSignature(const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection) const;

		// Conservatively finds visible parts, relevant casters, and their hash.
		void ComputeVisibleSet(
			Camera *camera,
			const std::shared_ptr<WorldRoot> &world,
			glm::vec3 lightDirection,
			bool needSignature,
			VisibleSet &out
		);

		const VisibleSet &EnsureVisibleSet(
			Camera *camera,
			const std::shared_ptr<WorldRoot> &world,
			glm::vec3 lightDirection,
			uint64_t cameraSignature,
			bool needSignature = false
		);

		// Draws resized camera targets in one dependency-ordered command buffer.
		void DrawOffscreen(const std::vector<DrawContext> &cameras);

		void DrawComposite(const std::vector<DrawContext> &cameras);

		// Direct camera inputs, including the active antialias pass.
		std::vector<Camera *> GetSampledCameras(Camera *camera);

		// Returns roots plus unlisted inputs, inputs first. Cycles read the prior frame.
		std::vector<Camera *> GetRenderOrder(const std::vector<Camera *> &roots);

		struct WindowRegion {
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
		};
		static WindowRegion ComputeWindowRegion(const Camera &camera, int windowWidth, int windowHeight);

		// Parks thread until readback; false means rendering could not start.
		bool RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine);
		void PollRenders(ThreadEngine *threadEngine);

		void SetAntialiasOverride(std::shared_ptr<ShaderScript> shader);

		// Top-left pixel overlay; null removes it. Excluded from camera caches.
		static constexpr size_t MAXIMUM_WINDOW_OVERLAYS = 2;
		void SetWindowOverlay(size_t slot, std::shared_ptr<EditableImage> image, glm::vec2 position);

		void Resize(int width, int height);
		void Destroy();
		void ReleaseCameraTarget(Camera *camera);

		static RenderProvider *GetCurrent();
		static void SetCurrent(RenderProvider *provider);

		// Last world, used by Camera:Render() calls outside the frame path.
		struct SceneContext {
			std::shared_ptr<WorldRoot> WorldRoot;
			glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));
			double Time = 0.0;
		};
		SceneContext Scene;

		uint64_t SceneSignature = 0;

		// Build lookup sets only when surface cameras make redraw checks need them.
		mutable bool WorldHasSurfaceCameras = false;
		mutable bool WorldHasSurfaces = false;

		mutable uint64_t SurfaceSignature = 0;
		uint64_t ResolvedSurfaceSignature = 0;
		bool PartTexturesResolved = false;
		// Invalidates stale texture pointers.
		uint64_t TargetGeneration = 1;

		struct PartRow {
			glm::vec3 Centre{0.0f};
			float Radius = 0.0f;
			bool CastShadow = false;
			// Present-and-what, so the frame's surface work skips the rest
			bool HasSurface = false;
			bool HasSurfaceCamera = false;
			Camera *SurfaceCamera = nullptr;
			EditableImage *SurfaceImage = nullptr;
			// The part's fixed contribution to the scene hash
			uint64_t StaticMix = 0;
		};
		mutable std::vector<PartRow> PartRows;
		mutable std::vector<uint16_t> PartRowHash;
		// The built instance, so the opaque pass copies rather than computes
		mutable std::vector<InstanceData> PartInstances;
		void SyncPartRows(const std::shared_ptr<WorldRoot> &world) const;

		// Reused culling chunk storage.
		struct CullResult {
			bool InView = false;
			bool ShadowReaches = false;
			bool CastsShadow = false;
		};
		std::vector<CullResult> CullScratch;

		SDL_Window *Window = nullptr;
		SDL_GPUDevice *Gpu = nullptr;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;
		SDL_GPUTexture *DepthTexture = nullptr;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler = nullptr;

		SDL_GPUTextureFormat SwapchainFormat;

		std::unique_ptr<RenderPass> ShadowPass;
		std::unique_ptr<RenderPass> OpaquePass;
		RenderPass *GetVelocityPass();
		std::unique_ptr<RenderPass> VelocityPass;
		// Separate pipeline because target format must match OFFSCREEN_FORMAT.
		std::unique_ptr<RenderPass> OffscreenOpaquePass;

	  private:
		struct PendingRender {
			lua_State *Thread = nullptr;
			int ThreadReference = LUA_NOREF;
			SDL_GPUFence *Fence = nullptr;
			SDL_GPUTransferBuffer *TransferBuffer = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::shared_ptr<EditableImage> Image;
		};

		struct CompiledShader {
			SDL_GPUGraphicsPipeline *GraphicsPipeline = nullptr;
			SDL_GPUComputePipeline *ComputePipeline = nullptr;
			ShaderReflection::BlockLayout ParameterLayout;
			// Reflected resources used to validate bindings.
			ShaderReflection::ResourceCounts Resources;
			bool Failed = false;
			// Eviction stamp; current-frame pipelines may still be bound.
			uint64_t LastUsedFrame = 0;
		};

		// Shader binding 0; reflection tracks only members each shader reads.
		struct alignas(16) BuiltinUniforms {
			glm::vec4 Resolution;
			glm::vec4 Time;
			// xy current jitter, zw prior jitter; zero unless requested.
			glm::vec4 Jitter;
		};

		struct UploadedImage {
			SDL_GPUTexture *Texture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint64_t Revision = 0;
		};

		std::unordered_map<Camera *, CameraTarget> CameraTargets;
		std::unordered_map<EditableImage *, UploadedImage> UploadedImages;
		std::vector<PendingRender> PendingRenders;

		// All draws submit here so frame pacing tracks their work.
		void SubmitTracked(SDL_GPUCommandBuffer *commands);

		// Returns the valid target; outRecorded is false when cache did all work.
		CameraTarget *RecordCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded);
		// False when no usable target or work remains.
		bool RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext);

		uint64_t ComputeCameraSignature(Camera *camera);

		// Remaining camera work for this frame.
		struct RedrawPlan {
			// Target already holds the final picture.
			bool Skip = false;
			// Redraw instead of restoring cache.
			bool RenderScene = true;
			// First chain pass to run.
			size_t FirstShader = 0;
			// Snapshot the cache cut.
			bool WriteCache = false;
		};
		// Delay cache copies until the picture settles.
		static constexpr uint32_t CACHE_AFTER_STILL_FRAMES = 5;

		RedrawPlan PlanRedraw(DrawContext &drawContext, CameraTarget &target);
		void EnsureCacheTexture(CameraTarget &target);

		std::unordered_set<Camera *> RedrawnThisFrame;

		// Monotonic target revisions used by cross-frame signatures.
		std::unordered_map<Camera *, uint64_t> CameraDrawCounts;
		uint64_t GetCameraDrawCount(Camera *camera) const;
		void CountCameraDraw(Camera *camera);

		std::vector<SDL_GPUFence *> FrameFences;
		std::deque<std::vector<SDL_GPUFence *>> FramesInFlight;
		// Waits before releasing fences and their referenced GPU resources.
		void RetireFrame(std::vector<SDL_GPUFence *> &fences);
		std::unordered_map<std::string, CompiledShader> ShaderCache;
		// Cached revision per runtime shader, used to release replacements.
		std::unordered_map<uint64_t, uint64_t> CachedShaderRevisions;

		static constexpr size_t MAXIMUM_CACHED_SHADERS = 128;

		// Advanced only by BeginFrame; bounds shader GPU lifetime.
		uint64_t FrameIndex = 0;

		CompiledShader *FindCachedShader(const std::string &key);
		// Returned reference survives only until the next insertion.
		CompiledShader &InsertCachedShader(const std::string &key, ShaderScript *shader);
		void ReleaseCachedShader(const std::string &key);
		// Exact removal prevents repeated recompiles from filling the cache.
		void DropSupersededShader(ShaderScript *shader);
		// Evict oldest, excluding current-frame entries.
		void TrimShaderCache();
		std::unordered_set<Camera *> ReportedCycles;
		// Cyclic readers and edges that use prior-frame targets.
		std::unordered_set<Camera *> NeedsHistory;
		std::set<std::pair<Camera *, Camera *>> HistoryEdges;
		SDL_GPUTexture *WhiteTexture = nullptr;
		std::unordered_map<const BasePart *, SDL_GPUTexture *> PartTextures;
		std::vector<SDL_GPUTexture *> SurfaceTextures;
		bool SurfaceSlotsComplete = false;
		// Cached per camera; dropped with its target.
		std::unordered_map<Camera *, VisibleSet> VisibleSets;

		// Later slots overlay earlier slots.
		struct WindowOverlayEntry {
			std::shared_ptr<EditableImage> Image;
			glm::vec2 Position = glm::vec2(0.0f);
		};
		std::array<WindowOverlayEntry, MAXIMUM_WINDOW_OVERLAYS> WindowOverlays;
		// Separate blending pipeline for the swapchain format.
		SDL_GPUGraphicsPipeline *WindowOverlayPipeline = nullptr;
		bool WindowOverlayFailed = false;
		void RecordWindowOverlay(
			SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
		);

		SDL_GPUShader *FullscreenVertexShader = nullptr;
		SDL_GPUShader *OpaqueVertexShader = nullptr;
		SDL_GPUSampler *ShaderSampler = nullptr;
		// Repeat for tiled surfaces; post-process sampling clamps separately.
		SDL_GPUSampler *PartSurfaceSampler = nullptr;
		// Point-sample motion; interpolation invents invalid vectors at edges.
		SDL_GPUSampler *PointSampler = nullptr;
		void EnsurePointSampler();
		SDL_GPUSampler *GetSourceSampler(const ShaderProperties::TextureSource &source);

		// Null for empty viewports; withScratch guarantees both ping-pong targets.
		CameraTarget *AcquireCameraTarget(Camera *camera, bool withScratch);
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
		);
		// Runs from firstShader and leaves output in ColorTexture; its input is ready.
		void RecordShaderChain(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, size_t firstShader, bool writeCache
		);

		// Camera passes plus active antialiasing.
		std::vector<std::shared_ptr<ShaderScript>> BuildShaderChain(Camera *camera);
		// First always-redraw pass; earlier passes are cacheable.
		static size_t FindCacheCut(const std::vector<std::shared_ptr<ShaderScript>> &chain);
		void ResolvePartTextures(const std::shared_ptr<WorldRoot> &worldRoot);
		void EnsureWhiteTexture();
		// RenderSettings override, else the built-in pass.
		std::shared_ptr<ShaderScript> GetAntialiasShader();
		std::shared_ptr<PostProcessShader> AntialiasShader;
		std::shared_ptr<ShaderScript> AntialiasOverride;

		SDL_GPUTexture *AcquireImageTexture(EditableImage *image);
		// Resolve image/camera input; cycle-closing readers use the prior frame.
		SDL_GPUTexture *ResolveTextureSource(Camera *reader, const ShaderProperties::TextureSource &source);
		void RecordHistoryCopy(SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTarget &target);

		// Optional temporal outputs requested by this camera's shaders.
		struct TemporalNeeds {
			// Copy finished output each frame for History sampling.
			bool History = false;
			// One extra pass writes Velocity and Depth together.
			bool Motion = false;
			// Copy this frame's depth for next frame.
			bool DepthHistory = false;
			// Move projection within the pixel each frame.
			bool Jitter = false;

			bool Any() const {
				return History || Motion || DepthHistory || Jitter;
			}
		};
		TemporalNeeds GetTemporalNeeds(Camera *camera);
		// Seed history from current output, never uninitialized driver memory.
		void EnsureTemporalTargets(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, const TemporalNeeds &needs
		);
		// Stamp after all cameras so they share one previous frame.
		void StampPreviousTransforms();
		// Track velocity demand and whether prior transforms are populated.
		bool VelocityInUse = false;
		bool TransformsStamped = false;
		// Cached per shader and target format.
		CompiledShader *GetSurfaceShader(SurfaceShader *shader, SDL_GPUTextureFormat colorFormat);
		bool PrepareSurfaceShader(
			FrameContext &frameContext,
			Camera *camera,
			SDL_GPUTextureFormat colorFormat,
			std::vector<uint8_t> &parameterStorage,
			std::vector<SDL_GPUTextureSamplerBinding> &samplerStorage
		);
		CompiledShader *GetPostProcessShader(PostProcessShader *shader);
		CompiledShader *GetComputeShader(ComputeShader *shader);
		static std::string GetShaderCacheKey(ShaderScript *shader, const char *stageExtension);
		void *LoadShaderBytes(const std::string &source, const char *stageExtension, size_t &outSize);
		// Pack by reflected name, or slot order as fallback.
		static std::vector<uint8_t> PackParameters(ShaderScript *shader, const CompiledShader &compiled);
	};
} // namespace gargantuan
