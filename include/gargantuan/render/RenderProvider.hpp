#pragma once

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
			// Last frame's picture, only for a camera something reads across a
			// sampling cycle
			SDL_GPUTexture *HistoryTexture = nullptr;
			// One geometry pass writes both, so they come and go together
			SDL_GPUTexture *VelocityTexture = nullptr;
			SDL_GPUTexture *ViewDepthTexture = nullptr;
			SDL_GPUTexture *ViewDepthHistoryTexture = nullptr;
			SDL_GPUTexture *DepthTexture = nullptr;
			// The cascading cache: the chain's output at the last pass before
			// the first RedrawEveryFrame one. A still scene reuses this and runs
			// only the passes after the cut.
			SDL_GPUTexture *CacheTexture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		// Blocks until only `maximumFramesInFlight - 1` frames are outstanding.
		// SDL keeps everything a submitted buffer touched alive until the GPU
		// drains it, so without the gate the backlog grows ~3.3 MB a frame.
		// Waiting on the frame just submitted would halve the frame rate, so a
		// small backlog is deliberate.
		void BeginFrame(int maximumFramesInFlight);
		void EndFrame();

		void Draw(DrawContext drawContext);
		// Whole-world fast check; ComputeVisibleSet narrows changes per camera.
		// Hashing instead of dirty flags keeps new visual properties from being
		// silently omitted.
		uint64_t ComputeSceneSignature(const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection) const;

		// Walks the frustum for the parts this camera can draw, those that could
		// throw a shadow in, and the hash over them. Conservative one way only:
		// a part can pass and still be invisible, costing a redraw, but nothing
		// visible is ever left out.
		void ComputeVisibleSet(
			Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, VisibleSet &out
		);

		const VisibleSet &EnsureVisibleSet(
			Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t cameraSignature
		);

		// Draws cameras into their own targets, resizing each to its
		// ViewportSize first. Takes the whole list because it all goes into one
		// command buffer, which is also what orders a camera reading another's
		// target.
		void DrawOffscreen(const std::vector<DrawContext> &cameras);

		void DrawComposite(const std::vector<DrawContext> &cameras);

		// Cameras this one samples directly. Walks the chain it actually runs,
		// antialias pass included, since a swapped-in pass binding a camera is a
		// dependency like any other.
		std::vector<Camera *> GetSampledCameras(Camera *camera);

		// Orders cameras so anything sampled is drawn first, pulling in
		// dependencies the caller did not list. A cycle cannot be satisfied, so
		// the edge closing it is dropped and that reader takes a frame-old
		// picture instead of deadlocking.
		std::vector<Camera *> GetRenderOrder(const std::vector<Camera *> &roots);

		struct WindowRegion {
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
		};
		static WindowRegion ComputeWindowRegion(const Camera &camera, int windowWidth, int windowHeight);

		// Parks `thread` until the offscreen download completes, then resumes it
		// with an EditableImage. False means the render could not start.
		bool RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine);
		void PollRenders(ThreadEngine *threadEngine);

		void SetAntialiasOverride(std::shared_ptr<ShaderScript> shader);

		// A picture laid over the finished window, in pixels from its top left;
		// null takes it away. Composited after every camera has been blitted to
		// the swapchain, so no camera target holds it and nothing about it
		// reaches the redraw signatures.
		static constexpr size_t MAXIMUM_WINDOW_OVERLAYS = 2;
		void SetWindowOverlay(size_t slot, std::shared_ptr<EditableImage> image, glm::vec2 position);

		void Resize(int width, int height);
		void Destroy();
		void ReleaseCameraTarget(Camera *camera);

		static RenderProvider *GetCurrent();
		static void SetCurrent(RenderProvider *provider);

		// Last frame's world. Camera:Render() can be called from anywhere, so it
		// reads the scene from here rather than being handed one.
		struct SceneContext {
			std::shared_ptr<WorldRoot> WorldRoot;
			glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));
			double Time = 0.0;
		};
		SceneContext Scene;

		uint64_t SceneSignature = 0;

		// Noticed while the scene signature is taken. Decides whether the
		// frustum walk builds its lookup sets at all: they serve only the redraw
		// check asking whether a screen is on screen, and in a place with no
		// screens that is a hash insert per part per camera per frame for a
		// question nobody asks.
		mutable bool WorldHasSurfaceCameras = false;
		mutable bool WorldHasSurfaces = false;

		mutable uint64_t SurfaceSignature = 0;
		uint64_t ResolvedSurfaceSignature = 0;
		bool PartTexturesResolved = false;
		// So a stale texture pointer in the map cannot outlive its texture
		uint64_t TargetGeneration = 1;

		// One chunk's worth of cull answers, kept between frames so the walk
		// allocates nothing
		struct CullResult {
			bool InView = false;
			bool ShadowReaches = false;
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
		// A second opaque pass built for OFFSCREEN_FORMAT; a pipeline's colour
		// format has to match the texture it draws into
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
			// What the shader declares, so bindings are checked rather than
			// guessed from the script
			ShaderReflection::ResourceCounts Resources;
			bool Failed = false;
			// Eviction goes by this. The current frame is off limits: its
			// pipeline may already be bound into an unsubmitted command buffer.
			uint64_t LastUsedFrame = 0;
		};

		// Handed to every shader at binding 0. A shader declares only the
		// members it reads, which is what lets one be added here without
		// touching a single existing asset.
		struct alignas(16) BuiltinUniforms {
			glm::vec4 Resolution;
			glm::vec4 Time;
			// xy where inside the pixel this frame was sampled, zw the frame
			// before. Zero unless a pass asked to jitter.
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

		// Every draw submits through here rather than
		// SDL_SubmitGPUCommandBuffer, or its work would never be waited on
		void SubmitTracked(SDL_GPUCommandBuffer *commands);

		// Records whatever this camera still owes, honouring the cascading
		// cache, and returns the target holding its picture. `outRecorded` is
		// false when the cache answered the whole thing; the target is still
		// good, which is what lets a window camera present the same pixels
		// again rather than redraw them.
		CameraTarget *RecordCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded);
		// False if the camera has no usable target or had nothing left to do
		bool RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext);

		uint64_t ComputeCameraSignature(Camera *camera);

		// What a camera has left to do this frame
		struct RedrawPlan {
			// Nothing at all: its target already holds the right picture
			bool Skip = false;
			// Redraw the world, rather than reusing the cached image
			bool RenderScene = true;
			// Where in the chain to start
			size_t FirstShader = 0;
			// Keep a copy at the cut for next frame
			bool WriteCache = false;
		};
		// Keeping the cache costs a full-size copy every frame it is taken, so
		// wait for the picture to settle before paying it
		static constexpr uint32_t CACHE_AFTER_STILL_FRAMES = 5;

		RedrawPlan PlanRedraw(DrawContext &drawContext, CameraTarget &target);
		void EnsureCacheTexture(CameraTarget &target);

		std::unordered_set<Camera *> RedrawnThisFrame;

		// How many times each camera has rewritten its target, ever -- a
		// camera's answer to the revision an EditableImage carries. Counted
		// rather than flagged per frame, since the signatures it feeds are
		// compared against the previous frame's, not read within this one.
		std::unordered_map<Camera *, uint64_t> CameraDrawCounts;
		uint64_t GetCameraDrawCount(Camera *camera) const;
		void CountCameraDraw(Camera *camera);

		std::vector<SDL_GPUFence *> FrameFences;
		std::deque<std::vector<SDL_GPUFence *>> FramesInFlight;
		// Waits before releasing fences and their referenced GPU resources.
		void RetireFrame(std::vector<SDL_GPUFence *> &fences);
		std::unordered_map<std::string, CompiledShader> ShaderCache;
		// Which revision of each runtime-compiled shader the cache holds, so the
		// one it replaces can be found again to release it
		std::unordered_map<uint64_t, uint64_t> CachedShaderRevisions;

		static constexpr size_t MAXIMUM_CACHED_SHADERS = 128;

		// Advanced only by BeginFrame; shader eviction uses it as GPU lifetime.
		uint64_t FrameIndex = 0;

		CompiledShader *FindCachedShader(const std::string &key);
		// The reference stays valid until the next insert, which is why every
		// caller finishes with the entry before asking for another
		CompiledShader &InsertCachedShader(const std::string &key, ShaderScript *shader);
		void ReleaseCachedShader(const std::string &key);
		// Exact rather than waiting for the entry to age out, which keeps a
		// script recompiling every frame from filling the cache on its own
		void DropSupersededShader(ShaderScript *shader);
		// Oldest first, skipping anything this frame has already handed out
		void TrimShaderCache();
		std::unordered_set<Camera *> ReportedCycles;
		// Cameras read across a cycle, and the exact reader-to-target edges that
		// use the previous-frame copy
		std::unordered_set<Camera *> NeedsHistory;
		std::set<std::pair<Camera *, Camera *>> HistoryEdges;
		SDL_GPUTexture *WhiteTexture = nullptr;
		std::unordered_map<const BasePart *, SDL_GPUTexture *> PartTextures;
		// One per camera, kept between frames so a still scene is not walked
		// again. Dropped with the camera's target.
		std::unordered_map<Camera *, VisibleSet> VisibleSets;

		// Drawn in slot order, so a panel in a later slot lands over an earlier
		// one
		struct WindowOverlayEntry {
			std::shared_ptr<EditableImage> Image;
			glm::vec2 Position = glm::vec2(0.0f);
		};
		std::array<WindowOverlayEntry, MAXIMUM_WINDOW_OVERLAYS> WindowOverlays;
		// Its own pipeline rather than a PostProcessShader because it draws onto
		// the swapchain, whose format is the window's rather than
		// OFFSCREEN_FORMAT, and blends rather than replacing
		SDL_GPUGraphicsPipeline *WindowOverlayPipeline = nullptr;
		bool WindowOverlayFailed = false;
		void RecordWindowOverlay(
			SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
		);

		SDL_GPUShader *FullscreenVertexShader = nullptr;
		SDL_GPUShader *OpaqueVertexShader = nullptr;
		SDL_GPUSampler *ShaderSampler = nullptr;
		// Repeats rather than clamping, so a part can tile its surface picture.
		// Kept apart from ShaderSampler because a post-process pass reading past
		// its own edge wants the edge, not the far side.
		SDL_GPUSampler *PartSurfaceSampler = nullptr;
		// Motion vectors are measurements, not a picture: averaging the ones
		// either side of an edge invents a step neither surface took, and a pass
		// reprojecting by it lands between the two.
		SDL_GPUSampler *PointSampler = nullptr;
		void EnsurePointSampler();
		SDL_GPUSampler *GetSourceSampler(const ShaderScript::TextureSource &source);

		// Sized to the camera's ViewportSize, or null when the viewport is
		// empty. `withScratch` also guarantees the second ping-pong texture
		// exists.
		CameraTarget *AcquireCameraTarget(Camera *camera, bool withScratch);
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
		);
		// Runs the chain from `firstShader` on, leaving the result in
		// ColorTexture. Passes before it are assumed to have already produced
		// what sits there, either this frame's render or the cached image.
		void RecordShaderChain(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, size_t firstShader, bool writeCache
		);

		// The camera's passes, its own plus the built-in antialias one
		std::vector<std::shared_ptr<ShaderScript>> BuildShaderChain(Camera *camera);
		// Index of the first pass marked RedrawEveryFrame, or the chain length
		// when none is. Everything before it is cacheable.
		static size_t FindCacheCut(const std::vector<std::shared_ptr<ShaderScript>> &chain);
		void ResolvePartTextures(const std::shared_ptr<WorldRoot> &worldRoot);
		void EnsureWhiteTexture();
		// Whatever RenderSettings was given, or the engine's own pass when it
		// was given nothing
		std::shared_ptr<ShaderScript> GetAntialiasShader();
		std::shared_ptr<PostProcessShader> AntialiasShader;
		std::shared_ptr<ShaderScript> AntialiasOverride;

		SDL_GPUTexture *AcquireImageTexture(EditableImage *image);
		// An image or another camera's output, whichever the script bound.
		// `reader` decides whether this edge is the one that closes a cycle and
		// so has to read last frame's copy.
		SDL_GPUTexture *ResolveTextureSource(Camera *reader, const ShaderScript::TextureSource &source);
		void RecordHistoryCopy(SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTarget &target);

		// What a camera's shaders want produced beyond the picture itself. All
		// off for a camera whose passes are ordinary, which keeps the cost of
		// the temporal machinery on the places that asked for it.
		struct TemporalNeeds {
			// Bound Enum.RenderTexture.History, so the finished picture is
			// copied aside each frame -- and the camera can never sit still,
			// since its own output is an input that changed
			bool History = false;
			// Bound Velocity, Depth or DepthHistory, so the scene is drawn a
			// second time into the motion vector and distance buffers. One flag
			// for all three because one pass writes both.
			bool Motion = false;
			// Bound DepthHistory, so this frame's distances are copied aside
			bool DepthHistory = false;
			// Asked for the sub-pixel offset, so the projection moves inside the
			// pixel each frame
			bool Jitter = false;

			bool Any() const {
				return History || Motion || DepthHistory || Jitter;
			}
		};
		TemporalNeeds GetTemporalNeeds(Camera *camera);
		// History is seeded from the picture already in the target rather than
		// whatever the driver handed back, so the first frame of a reprojecting
		// pass blends against something plausible instead of noise.
		void EnsureTemporalTargets(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, const TemporalNeeds &needs
		);
		// At the end of the frame rather than as each camera draws, so every
		// camera measures motion against the same previous positions
		void StampPreviousTransforms();
		// Whether any camera drew motion vectors this frame, and whether the
		// parts are currently carrying a previous position. The pair lets the
		// stamp stop when nothing wants it and clear up after itself.
		bool VelocityInUse = false;
		bool TransformsStamped = false;
		// Cached per shader and colour format, since the window and an offscreen
		// target do not share one
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
		// By name where the layout is known, by slot order where it is not
		static std::vector<uint8_t> PackParameters(ShaderScript *shader, const CompiledShader &compiled);
	};
} // namespace gargantuan
