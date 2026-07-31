#pragma once

#include "gargantuan/render/InstanceData.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/render/SceneIndex.hpp"
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
		static constexpr SDL_GPUTextureFormat VELOCITY_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;

		static constexpr SDL_GPUTextureFormat VIEW_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		static RenderProvider *GetCurrent();
		static void SetCurrent(RenderProvider *provider);

		struct SceneContext {
			std::shared_ptr<WorldRoot> WorldRoot;
			glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));
			double TimeSeconds = 0.0;
		};
		// Published so an ad-hoc Camera:Render() draws the same world the frame did.
		SceneContext Scene;

		// Cameras compare against this to decide whether a still scene needs
		// redrawing, so the frame stamps it before drawing anything.
		void UpdateSceneSignature(const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection);

		// Cameras a part is showing on its surface; they have to be drawn
		// before whatever reads them.
		const std::vector<Camera *> &GetSurfaceCameras() const;

		void BeginFrame(int maximumFramesInFlight);
		void EndFrame();

		void Draw(DrawContext drawContext);
		void DrawOffscreen(const std::vector<DrawContext> &cameras);
		void DrawComposite(const std::vector<DrawContext> &cameras);

		std::vector<Camera *> GetDirectlySampledCameras(Camera *camera);

		std::vector<Camera *> GetRenderOrder(const std::vector<Camera *> &roots);

		const std::unordered_set<Camera *> &GetDemandedCameras(
			const std::vector<Camera *> &viewers, float fieldOfViewMarginFraction
		);

		struct WindowRegion {
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
		};
		static WindowRegion ComputeWindowRegion(const Camera &camera, int windowWidth, int windowHeight);

		bool BeginRenderReadback(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine);
		void ResumeCompletedReadbacks(ThreadEngine *threadEngine);

		void SetAntialiasOverride(std::shared_ptr<ShaderScript> shader);

		static constexpr size_t MAXIMUM_WINDOW_OVERLAYS = 2;
		void SetWindowOverlay(size_t overlayIndex, std::shared_ptr<EditableImage> image, glm::vec2 position);

		bool ShouldPresentUncapped = false;
		void Resize(int width, int height);
		void Destroy();
		void ReleaseCameraTarget(Camera *camera);

	  private:
		static constexpr SDL_GPUTextureFormat OFFSCREEN_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		// Every texture a camera can draw into or read back from this frame.
		struct CameraTextureSet {
			SDL_GPUTexture *ColorTexture = nullptr;
			SDL_GPUTexture *ChainPingPongTexture = nullptr;
			SDL_GPUTexture *HistoryTexture = nullptr;
			SDL_GPUTexture *VelocityTexture = nullptr;
			SDL_GPUTexture *ViewDepthTexture = nullptr;
			SDL_GPUTexture *ViewDepthHistoryTexture = nullptr;
			SDL_GPUTexture *DepthStencilAttachmentTexture = nullptr;
			SDL_GPUTexture *CachedChainPrefixTexture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		struct PendingRender {
			lua_State *ParkedThread = nullptr;
			int ThreadReference = LUA_NOREF;
			SDL_GPUFence *Fence = nullptr;
			SDL_GPUTransferBuffer *ReadbackTransferBuffer = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::shared_ptr<EditableImage> Image;
		};

		struct CompiledShader {
			SDL_GPUGraphicsPipeline *GraphicsPipeline = nullptr;
			SDL_GPUComputePipeline *ComputePipeline = nullptr;
			ShaderReflection::BlockLayout ParameterLayout;
			ShaderReflection::ResourceCounts ResourceCounts;
			bool DidCompileFail = false;
			uint64_t LastUsedFrame = 0;
		};

		struct alignas(16) BuiltinUniforms {
			glm::vec4 Resolution;
			glm::vec4 Time;
			glm::vec4 Jitter;
		};

		struct UploadedImage {
			SDL_GPUTexture *Texture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint64_t UploadedSourceRevision = 0;
		};

		// What to draw, and what has changed since it was last drawn.
		SceneIndex SceneDrawIndex;

		SDL_Window *Window = nullptr;
		SDL_GPUDevice *Gpu = nullptr;
		SDL_GPUTexture *DepthTexture = nullptr;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler = nullptr;

		SDL_GPUTextureFormat SwapchainFormat;

		std::unique_ptr<RenderPass> ShadowPass;
		std::unique_ptr<RenderPass> OpaquePass;
		std::unique_ptr<RenderPass> VelocityPass;
		std::unique_ptr<RenderPass> OffscreenOpaquePass;
		RenderPass *GetVelocityPass();

		// Bumped whenever camera targets are recreated, which invalidates every
		// surface that was reading one of them.
		uint64_t CameraTextureGeneration = 1;
		uint64_t ResolvedSurfaceSignature = 0;
		bool PartTexturesResolved = false;

		std::unordered_map<Camera *, CameraTextureSet> CameraTargets;
		std::unordered_map<EditableImage *, UploadedImage> UploadedImages;
		std::vector<PendingRender> PendingRenders;

		void SubmitAndTrackFence(SDL_GPUCommandBuffer *commands);

		// Teardown, each defined beside the code that allocates.
		void ReleaseTargetTextures(CameraTextureSet &target);
		void ReleaseCameraResources();
		void ReleaseTextureUploads();
		void ReleaseReadbacks();
		void ReleaseShaderCache();
		void ReleaseWindowOverlay();

		CameraTextureSet *RecordCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded);
		bool RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext);

		uint64_t ComputeCameraSignature(Camera *camera);

		struct RedrawPlan {
			bool ShouldSkipCamera = false;
			bool RenderScene = true;
			size_t FirstShaderChainIndex = 0;
			bool ShouldWriteCacheTexture = false;
		};
		static constexpr uint32_t CACHE_AFTER_STILL_FRAMES = 5;

		RedrawPlan PlanRedraw(DrawContext &drawContext, CameraTextureSet &target);
		void EnsureCacheTexture(CameraTextureSet &target);

		std::unordered_set<Camera *> RedrawnThisFrame;

		std::vector<SDL_GPUFence *> FrameFences;
		std::deque<std::vector<SDL_GPUFence *>> FramesInFlight;
		void RetireFrame(std::vector<SDL_GPUFence *> &fences);
		std::unordered_map<std::string, CompiledShader> ShaderCache;
		std::unordered_map<uint64_t, uint64_t> LastSeenRevisionByShaderSerial;

		static constexpr size_t MAXIMUM_CACHED_SHADERS = 128;

		uint64_t FrameIndex = 0;

		CompiledShader *FindCachedShader(const std::string &key);
		CompiledShader &InsertCachedShader(const std::string &key, ShaderScript *shader);
		void ReleaseCachedShader(const std::string &key);
		void DropSupersededShader(ShaderScript *shader);
		void TrimShaderCache();
		std::unordered_set<Camera *> ReportedCycles;
		std::unordered_set<Camera *> DemandedCameras;
		std::vector<Camera *> DemandStack;
		std::unordered_set<Camera *> CamerasNeedingHistory;
		std::set<std::pair<Camera *, Camera *>> PriorFrameReaderToSourceEdges;
		SDL_GPUTexture *WhiteTexture = nullptr;
		std::unordered_map<const BasePart *, SDL_GPUTexture *> PartTextures;
		std::vector<SDL_GPUTexture *> SurfaceTexturesBySlot;
		bool AllSurfacesGotSlots = false;

		struct WindowOverlayEntry {
			std::shared_ptr<EditableImage> Image;
			glm::vec2 Position = glm::vec2(0.0f);
		};
		std::array<WindowOverlayEntry, MAXIMUM_WINDOW_OVERLAYS> WindowOverlays;
		SDL_GPUGraphicsPipeline *WindowOverlayPipeline = nullptr;
		bool WindowOverlayFailed = false;
		bool EnsureWindowOverlayPipeline();
		void RecordWindowOverlay(
			SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
		);

		SDL_GPUShader *FullscreenVertexShader = nullptr;
		SDL_GPUShader *OpaqueVertexShader = nullptr;
		SDL_GPUSampler *ShaderSampler = nullptr;
		SDL_GPUSampler *PartSurfaceSampler = nullptr;
		SDL_GPUSampler *PointSampler = nullptr;
		void EnsurePointSampler();
		SDL_GPUSampler *GetTextureSourceSampler(const ShaderProperties::TextureSource &source);

		void BindSceneToFrame(FrameContext &frameContext, const DrawContext &drawContext);

		CameraTextureSet *AcquireCameraTarget(Camera *camera, bool needsPingPongTexture);
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTextureSet &target
		);
		void RecordShaderChain(
			SDL_GPUCommandBuffer *commands,
			Camera *camera,
			CameraTextureSet &target,
			size_t firstShaderIndex,
			bool shouldSnapshotChainPrefix
		);

		std::vector<std::shared_ptr<ShaderScript>> BuildShaderChain(Camera *camera);
		static size_t FindFirstAlwaysRedrawShaderIndex(const std::vector<std::shared_ptr<ShaderScript>> &chain);
		void ResolvePartTextures(const std::shared_ptr<WorldRoot> &worldRoot);
		void EnsureWhiteTextureAndSamplers();
		std::shared_ptr<ShaderScript> GetAntialiasShader();
		std::shared_ptr<PostProcessShader> AntialiasShader;
		std::shared_ptr<ShaderScript> AntialiasOverride;

		SDL_GPUTexture *GetOrUploadImageTexture(EditableImage *image);
		SDL_GPUTexture *ResolveTextureSource(Camera *reader, const ShaderProperties::TextureSource &source);
		void RecordTemporalHistoryCopies(SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTextureSet &target);

		struct TemporalNeeds {
			bool History = false;
			bool NeedsVelocityAndViewDepth = false;
			bool DepthHistory = false;
			bool Jitter = false;

			bool Any() const {
				return History || NeedsVelocityAndViewDepth || DepthHistory || Jitter;
			}
		};
		TemporalNeeds GetTemporalNeeds(Camera *camera);
		void EnsureTemporalTargets(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTextureSet &target, const TemporalNeeds &needs
		);
		void StampPreviousTransforms();
		bool IsVelocityPassUsedThisFrame = false;
		bool TransformsStamped = false;
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
		void *LoadShaderBytes(const std::string &sourceAssetName, const char *stageExtension, size_t &outSize);
		std::vector<uint8_t> LoadShaderBytecode(ShaderScript *shader, const char *stageExtension);
		SDL_GPUShader *LoadBuiltinVertexShader(const char *name, uint32_t uniformBufferCount);
		static std::vector<uint8_t> PackParameters(ShaderScript *shader, const CompiledShader &compiled);
	};
}
