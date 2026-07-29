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
	// Draws into a fixed format of its own, so unlike the opaque pass it needs
	// no swapchain format to match
	std::unique_ptr<RenderPass> CreateVelocityPass(SDL_GPUDevice *gpu);

	class RenderProvider {
	  public:
		// Offscreen cameras render at a fixed format so one extra pipeline
		// covers them all, whatever the window's swapchain happens to be
		static constexpr SDL_GPUTextureFormat OFFSCREEN_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		// Motion vectors are a signed screen-space step, so the eight-bit
		// unsigned format the pictures use cannot hold them. Two half floats
		// carry sub-pixel motion across the whole target and cost half of what
		// full floats would.
		static constexpr SDL_GPUTextureFormat VELOCITY_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;

		// Distance from the camera in studs, out to the far plane, which is a
		// range no fixed-point format covers and half floats run out of before
		// reaching. Full floats also make the copy kept for the next frame an
		// ordinary picture-to-picture blit, which a depth-stencil texture would
		// not have been.
		static constexpr SDL_GPUTextureFormat VIEW_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

		// The colour and depth textures backing one offscreen camera.
		// ScratchTexture is the other half of the ping-pong pair the shader
		// chain bounces through, and is only created once a camera has shaders.
		struct CameraTarget {
			SDL_GPUTexture *ColorTexture = nullptr;
			SDL_GPUTexture *ScratchTexture = nullptr;
			// Last frame's finished picture. Only allocated for a camera that
			// something reads across a sampling cycle, where no ordering can
			// give every reader this frame's copy.
			SDL_GPUTexture *HistoryTexture = nullptr;
			// Where every pixel was last frame, and how far away each one is
			// now. Only allocated for a camera whose chain bound
			// Enum.RenderTexture.Velocity, .Depth or .DepthHistory; the two
			// come out of one geometry pass that only such a camera records, so
			// they are allocated and dropped together.
			SDL_GPUTexture *VelocityTexture = nullptr;
			SDL_GPUTexture *ViewDepthTexture = nullptr;
			// Last frame's distances, kept only when something asks. Velocity
			// says where a pixel was; this says what was standing there.
			SDL_GPUTexture *ViewDepthHistoryTexture = nullptr;
			SDL_GPUTexture *DepthTexture = nullptr;
			// The cascading cache: the chain's output at the last pass before
			// the first one marked RedrawEveryFrame. On a still scene the
			// engine reuses this instead of redrawing the world, and runs only
			// the passes from that point on. Only allocated for a camera whose
			// chain actually has such a pass; without one the whole camera is
			// skipped and ColorTexture already holds the answer.
			SDL_GPUTexture *CacheTexture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		// SDL keeps everything a submitted command buffer touched alive until
		// the GPU has finished with it -- the buffer itself, its descriptor
		// pools, its uniform buffers. Nothing in a frame waits, so without a
		// gate the engine submits far faster than the GPU drains and that
		// backlog grows without bound: measured at 3.3 MB a frame, which is
		// gigabytes within seconds.
		//
		// BeginFrame blocks until only `maximumFramesInFlight - 1` frames are
		// still outstanding, which paces the CPU to the GPU. Waiting on the
		// frame just submitted would serialise the two and halve the frame
		// rate, so a small backlog is deliberate; RenderSettings.FramesInFlight
		// is where the number comes from.
		void BeginFrame(int maximumFramesInFlight);
		// Closes the frame, so the fences submitted since BeginFrame are what
		// a later BeginFrame waits on
		void EndFrame();

		// Draws a camera to the window
		void Draw(DrawContext drawContext);
		// A cheap hash of everything in the world that changes a picture --
		// every part's transform, size and appearance, plus the light. Cameras
		// compare it against the one they last drew at, so a scene that has not
		// moved is not redrawn.
		//
		// Hashing the state each frame rather than dirtying a flag from every
		// setter means nothing can be forgotten: a property added later is
		// covered the moment it joins the hash, not whenever someone remembers
		// to mark it.
		//
		// This one covers the whole world, so it answers "did anything at all
		// move" for every camera at once. ComputeVisibleSceneSignature narrows
		// it to one camera when the answer is yes.
		uint64_t ComputeSceneSignature(const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection) const;

		// Walks the camera's frustum, recording both which parts it can draw
		// and the hash over them. Only the parts that fall inside, plus those
		// that could throw a shadow in, are mixed: a part outside both cannot
		// change this camera's picture however much it moves, so it is left
		// out and the camera keeps its cache.
		//
		// Conservative in one direction only. A part can pass the test and
		// still be invisible -- hidden behind a wall, or its bounding sphere
		// clipping a corner the part itself misses -- which costs a redraw
		// that was not needed. Nothing visible is ever left out, so the
		// picture is never wrong.
		void ComputeVisibleSet(
			Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, VisibleSet &out
		);

		// The camera's visible set, walked only when the world or the camera
		// has moved since the last one. The redraw check asks for it to decide
		// whether to draw at all, and the passes ask again to decide what to
		// submit; the second ask is free whenever the first already happened.
		const VisibleSet &EnsureVisibleSet(
			Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t cameraSignature
		);

		// Draws cameras into their own offscreen targets, creating or resizing
		// each target to match its camera's ViewportSize first.
		//
		// Takes the whole list rather than one camera at a time because all of
		// it goes into a single command buffer: a submission carries real
		// driver cost, and one camera reading another's target needs the two
		// ordered anyway, which recording order already guarantees.
		void DrawOffscreen(const std::vector<DrawContext> &cameras);

		// Draws several cameras into one window, each into its own rectangle.
		// Split-screen: every camera renders offscreen, then its target is
		// blitted into place.
		void DrawComposite(const std::vector<DrawContext> &cameras);

		// Cameras a camera samples through its shaders, directly. Walks the
		// chain the camera actually runs, antialias pass included, rather than
		// only the passes a script added: a swapped-in pass that binds a camera
		// is a dependency like any other, and one that binds its own camera is
		// the cycle that earns it a previous-frame copy.
		std::vector<Camera *> GetSampledCameras(Camera *camera);

		// Orders cameras so that anything sampled by another is drawn before
		// it, pulling in dependencies the caller did not list. Without this a
		// camera reading another's target would see the previous frame.
		//
		// A cycle cannot be satisfied; the edge that closes it is dropped, so
		// that one camera reads a frame-old picture instead of deadlocking.
		std::vector<Camera *> GetRenderOrder(const std::vector<Camera *> &roots);

		// Where a camera lands in a window of this size, in pixels. Pure, so
		// the layout can be checked without a GPU.
		struct WindowRegion {
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
		};
		static WindowRegion ComputeWindowRegion(const Camera &camera, int windowWidth, int windowHeight);

		// Renders the camera offscreen, starts a download of the result, and
		// parks `thread` until it lands. The thread is resumed with an
		// EditableImage. Returns false if the render could not be started.
		bool RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine);
		// Resumes any threads whose downloads have finished
		void PollRenders(ThreadEngine *threadEngine);

		// Hands over what RenderSettings.AntialiasShader currently holds. The
		// engine pushes it rather than the renderer reaching for the service,
		// which is how the rest of RenderSettings already travels.
		void SetAntialiasOverride(std::shared_ptr<ShaderScript> shader);

		// A picture laid over the finished window, in pixels from its top left.
		// Passing null takes it away again.
		//
		// It goes onto the swapchain after every camera has been blitted there,
		// which is what keeps it out of everything else: no camera's target
		// holds it, Camera:Render() does not hand it back, and nothing about it
		// reaches the redraw signatures. A debug readout that changed what the
		// engine decided to redraw would be measuring itself.
		static constexpr size_t MAXIMUM_WINDOW_OVERLAYS = 2;
		void SetWindowOverlay(size_t slot, std::shared_ptr<EditableImage> image, glm::vec2 position);

		void Resize(int width, int height);
		void Destroy();
		// Drops the target belonging to a camera that is going away
		void ReleaseCameraTarget(Camera *camera);

		// The provider the engine is currently driving, so that Luau-facing
		// code can reach it without threading a pointer through every class
		static RenderProvider *GetCurrent();
		static void SetCurrent(RenderProvider *provider);

		// What the world looked like on the last frame. Camera:Render() can be
		// called from anywhere, so it reads the scene from here rather than
		// being handed one.
		struct SceneContext {
			std::shared_ptr<WorldRoot> WorldRoot;
			glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));
			// Seconds the place has been running, handed to shaders as a builtin
			double Time = 0.0;
		};
		SceneContext Scene;

		// Hash of everything in the world that changes a picture, refreshed by
		// the engine once a frame and compared per camera
		uint64_t SceneSignature = 0;

		SDL_Window *Window = nullptr;
		SDL_GPUDevice *Gpu = nullptr;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;
		SDL_GPUTexture *DepthTexture = nullptr;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler = nullptr;

		SDL_GPUTextureFormat SwapchainFormat;

		std::unique_ptr<RenderPass> ShadowPass;
		std::unique_ptr<RenderPass> OpaquePass;
		// Built lazily, on the first camera that asks for motion vectors: a
		// place that never wants them should not pay for the pipeline either
		RenderPass *GetVelocityPass();
		std::unique_ptr<RenderPass> VelocityPass;
		// A second opaque pass built for OFFSCREEN_FORMAT; a pipeline's colour
		// format has to match the texture it draws into
		std::unique_ptr<RenderPass> OffscreenOpaquePass;

	  private:
		// A download in flight, waiting on the GPU to signal its fence
		struct PendingRender {
			lua_State *Thread = nullptr;
			int ThreadReference = LUA_NOREF;
			SDL_GPUFence *Fence = nullptr;
			SDL_GPUTransferBuffer *TransferBuffer = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::shared_ptr<EditableImage> Image;
		};

		// What one shader asset compiled down to. Shaders are named, so they
		// are cached by name and shared between every camera using them.
		struct CompiledShader {
			SDL_GPUGraphicsPipeline *GraphicsPipeline = nullptr;
			SDL_GPUComputePipeline *ComputePipeline = nullptr;
			// Where each named parameter goes, read out of the SPIR-V. Without
			// it the engine falls back to packing in Set order.
			ShaderReflection::BlockLayout ParameterLayout;
			// What the shader itself asks for, so bindings are checked against
			// the declaration rather than guessed from the script
			ShaderReflection::ResourceCounts Resources;
			// Set once a compile has been attempted and failed, so the engine
			// complains once rather than every frame
			bool Failed = false;
			// The frame this was last handed out on. Eviction goes by it, and
			// anything from the current frame is off limits: its pipeline may
			// already be bound into a command buffer that has not been
			// submitted yet.
			uint64_t LastUsedFrame = 0;
		};

		// What the shaders are handed alongside their own parameters.
		//
		// Every shader declares the block it wants and reads it at binding 0;
		// one declaring only Resolution and Time is handed the same buffer and
		// simply never looks past what it named, which is what lets a member be
		// added here without touching a single existing asset.
		struct alignas(16) BuiltinUniforms {
			glm::vec4 Resolution;
			glm::vec4 Time;
			// xy is where inside the pixel this frame was sampled, in pixels,
			// and zw where the frame before it was. Zero on a camera with no
			// pass asking to jitter, which is nearly all of them.
			glm::vec4 Jitter;
		};

		// An EditableImage that has been copied onto the GPU so a shader can
		// sample it, kept until the image changes underneath it
		struct UploadedImage {
			SDL_GPUTexture *Texture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint64_t Revision = 0;
		};

		std::unordered_map<Camera *, CameraTarget> CameraTargets;
		std::unordered_map<EditableImage *, UploadedImage> UploadedImages;
		std::vector<PendingRender> PendingRenders;

		// Submits a command buffer and counts it against the frame's budget.
		// Every draw goes through here rather than SDL_SubmitGPUCommandBuffer,
		// or its work would never be waited on.
		void SubmitTracked(SDL_GPUCommandBuffer *commands);

		// Records whatever this camera still owes for the frame into an
		// already-acquired command buffer, honouring the cascading cache.
		// Returns the target holding its finished picture, or null when the
		// camera has no usable one.
		//
		// `outRecorded` is false when the cache answered the whole thing and
		// nothing was written into the buffer. That is not a failure: the
		// target is still good, which is what lets a camera drawing to the
		// window present the same pixels again rather than redraw them.
		CameraTarget *RecordCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded);
		// Records one camera into an already-acquired command buffer. False if
		// the camera has no usable target or had nothing left to do.
		bool RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext);

		// Everything about a camera that changes its picture without the world
		// moving: where it points, what it shows, and the shaders over it down
		// to their parameter values
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
		// Frames a camera has to sit perfectly still before the engine commits
		// to caching it. Keeping the cache costs a full-size copy every frame
		// it is taken, so paying that on a picture that is about to change
		// again is worse than simply redrawing; waiting for the scene to settle
		// means the cost is only paid where it earns something back.
		static constexpr uint32_t CACHE_AFTER_STILL_FRAMES = 5;

		RedrawPlan PlanRedraw(DrawContext &drawContext, CameraTarget &target);
		// Allocated lazily: only a camera with an animated tail ever needs one
		void EnsureCacheTexture(CameraTarget &target);

		// Cameras redrawn this frame. One sampling another has to follow it,
		// or it would composite this frame's picture over last frame's.
		std::unordered_set<Camera *> RedrawnThisFrame;

		// How many times each camera has rewritten its target, ever. This is a
		// camera's answer to the revision an EditableImage carries: what a part
		// shows on its surface is one or the other, and a camera that has drawn
		// again has changed the part's appearance exactly as a drawn-into image
		// would have.
		//
		// Counted rather than flagged per frame because the signatures it feeds
		// are compared against the previous frame's, not read within this one.
		// A flag would depend on whether the camera doing the looking happened
		// to be recorded before or after the camera being looked at; a number
		// that only ever goes up does not.
		std::unordered_map<Camera *, uint64_t> CameraDrawCounts;
		uint64_t GetCameraDrawCount(Camera *camera) const;
		// Called wherever a camera's target is actually rewritten
		void CountCameraDraw(Camera *camera);

		std::vector<SDL_GPUFence *> FrameFences;
		std::deque<std::vector<SDL_GPUFence *>> FramesInFlight;
		// Waits on a frame's fences and releases them
		void RetireFrame(std::vector<SDL_GPUFence *> &fences);
		// Every pipeline built so far, by cache key. Bounded two ways: a script
		// recompiling a shader drops the revision it replaced straight away,
		// and whatever is left is trimmed to MAXIMUM_CACHED_SHADERS by age.
		std::unordered_map<std::string, CompiledShader> ShaderCache;
		// Which revision of each runtime-compiled shader the cache is holding,
		// so the one it replaces can be found again to release it
		std::unordered_map<uint64_t, uint64_t> CachedShaderRevisions;

		// Generous, since an entry is a pipeline and a layout rather than
		// anything large, and a place using more distinct shaders than this at
		// once would be thrashing whatever the number was.
		static constexpr size_t MAXIMUM_CACHED_SHADERS = 128;

		// Counts frames so eviction can tell what is still in use. Only
		// BeginFrame moves it.
		uint64_t FrameIndex = 0;

		// Marks the entry used this frame and hands it back, or null when the
		// key is absent
		CompiledShader *FindCachedShader(const std::string &key);
		// Makes room, then inserts. Returns a reference that stays valid until
		// the next insert, which is why every caller finishes with the entry
		// before asking for another.
		CompiledShader &InsertCachedShader(const std::string &key, ShaderScript *shader);
		// Releases one entry's GPU pipelines and erases it
		void ReleaseCachedShader(const std::string &key);
		// Drops the revision a recompile just replaced. Exact rather than
		// waiting for the entry to age out, which is what keeps a script that
		// recompiles every frame from filling the cache on its own.
		void DropSupersededShader(ShaderScript *shader);
		// Trims the cache back to its bound, oldest first, skipping anything
		// this frame has already handed out
		void TrimShaderCache();
		// Cycles are reported once rather than every frame
		std::unordered_set<Camera *> ReportedCycles;
		// Cameras read across a cycle, which therefore keep a previous-frame
		// copy, and the exact reader-to-target edges that use it
		std::unordered_set<Camera *> NeedsHistory;
		std::set<std::pair<Camera *, Camera *>> HistoryEdges;
		// A single white pixel, so a part with no surface camera multiplies by
		// one instead of needing a second pipeline
		SDL_GPUTexture *WhiteTexture = nullptr;
		std::unordered_map<const BasePart *, SDL_GPUTexture *> PartTextures;
		// One per camera, kept between frames so a still scene is not walked
		// again. Dropped with the camera's target.
		std::unordered_map<Camera *, VisibleSet> VisibleSets;

		// What SetWindowOverlay was last given, and where each one goes. Drawn
		// in slot order, so a panel in a later slot lands over an earlier one.
		struct WindowOverlayEntry {
			std::shared_ptr<EditableImage> Image;
			glm::vec2 Position = glm::vec2(0.0f);
		};
		std::array<WindowOverlayEntry, MAXIMUM_WINDOW_OVERLAYS> WindowOverlays;
		// Built on the first frame something is actually laid over the window,
		// and never at all in a place that shows no overlay. Its own pipeline
		// rather than a PostProcessShader because it draws onto the swapchain,
		// whose format is the window's rather than OFFSCREEN_FORMAT, and blends
		// rather than replacing.
		SDL_GPUGraphicsPipeline *WindowOverlayPipeline = nullptr;
		bool WindowOverlayFailed = false;
		// Composites it onto whatever is already on `target`
		void RecordWindowOverlay(
			SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
		);

		SDL_GPUShader *FullscreenVertexShader = nullptr;
		SDL_GPUShader *OpaqueVertexShader = nullptr;
		SDL_GPUSampler *ShaderSampler = nullptr;
		// Repeats rather than clamping, which is what lets a part tile its
		// surface picture. Kept apart from ShaderSampler because a post-process
		// pass reading past its own edge wants the edge, not the far side.
		SDL_GPUSampler *PartSurfaceSampler = nullptr;
		// Motion vectors are measurements, not a picture: averaging the ones
		// either side of an edge invents a step neither surface took, and a
		// pass reprojecting by it lands between the two. Point sampling gives
		// back exactly what was written.
		SDL_GPUSampler *PointSampler = nullptr;
		void EnsurePointSampler();
		// Which of the two a binding should be read through
		SDL_GPUSampler *GetSourceSampler(const ShaderScript::TextureSource &source);

		// Returns the camera's target, sized to its ViewportSize, or nullptr
		// when the viewport is empty. `withScratch` also guarantees the second
		// ping-pong texture exists.
		CameraTarget *AcquireCameraTarget(Camera *camera, bool withScratch);
		// Records the shadow and opaque passes for one camera into `commands`
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
		);
		// Runs the camera's shader chain, leaving the result in ColorTexture
		// Runs the chain from `firstShader` on. Passes before it are assumed to
		// have already produced what sits in the camera's colour texture, which
		// is either this frame's render or the cached image.
		void RecordShaderChain(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, size_t firstShader, bool writeCache
		);

		// The camera's passes, its own plus the built-in antialias one
		std::vector<std::shared_ptr<ShaderScript>> BuildShaderChain(Camera *camera);
		// Index of the first pass marked RedrawEveryFrame, or the chain length
		// when none is. Everything before it is cacheable.
		static size_t FindCacheCut(const std::vector<std::shared_ptr<ShaderScript>> &chain);
		// Works out which texture each part shows this frame
		void ResolvePartTextures(const std::shared_ptr<WorldRoot> &worldRoot);
		void EnsureWhiteTexture();
		// The one shared instance of the built-in antialias pass
		// What Camera.Antialiasing actually runs: whatever RenderSettings was
		// given, or the engine's own pass when it was given nothing
		std::shared_ptr<ShaderScript> GetAntialiasShader();
		std::shared_ptr<PostProcessShader> AntialiasShader;
		std::shared_ptr<ShaderScript> AntialiasOverride;

		// Both prefer the script's runtime-compiled bytecode and fall back to
		// its named build-time asset
		// Uploads or refreshes the GPU copy of an image, returning null when it
		// is empty or the upload failed
		SDL_GPUTexture *AcquireImageTexture(EditableImage *image);
		// An image or another camera's output, whichever the script bound
		// `reader` decides whether this edge is the one that closes a cycle and
		// so has to read last frame's copy
		SDL_GPUTexture *ResolveTextureSource(Camera *reader, const ShaderScript::TextureSource &source);
		// Keeps a camera's previous-frame copy up to date, once it is done
		void RecordHistoryCopy(SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTarget &target);

		// What a camera's shaders want the renderer to produce for them beyond
		// the picture itself. All three are off for a camera whose passes are
		// ordinary, which is what keeps the cost of the temporal machinery on
		// the places that asked for it.
		struct TemporalNeeds {
			// A pass bound Enum.RenderTexture.History, so the finished picture
			// is copied aside each frame -- and the camera can never sit still,
			// since its own output is an input that changed
			bool History = false;
			// A pass bound Enum.RenderTexture.Velocity, .Depth or
			// .DepthHistory, so the scene is drawn a second time into the
			// motion vector and distance buffers. One flag for all three
			// because one pass writes both buffers at once.
			bool Motion = false;
			// A pass bound Enum.RenderTexture.DepthHistory, so this frame's
			// distances are copied aside once it is done with them
			bool DepthHistory = false;
			// A pass asked for the sub-pixel offset, so the projection moves
			// inside the pixel each frame
			bool Jitter = false;

			bool Any() const {
				return History || Motion || DepthHistory || Jitter;
			}
		};
		TemporalNeeds GetTemporalNeeds(Camera *camera);
		// Creates whatever `needs` asks for that is missing. History is seeded
		// from the picture already in the target rather than left as whatever
		// the driver handed back, so the first frame of a reprojecting pass
		// blends against something plausible instead of noise.
		void EnsureTemporalTargets(
			SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, const TemporalNeeds &needs
		);
		// Records where every part stood, once the frame is over, so the next
		// one can measure motion against it. Every camera in a frame has to
		// compare against the same previous positions, which is why this
		// happens at the end of the frame rather than as each camera draws.
		void StampPreviousTransforms();
		// Whether any camera drew motion vectors this frame, and whether the
		// parts are currently carrying a previous position. The pair is what
		// lets the stamp stop when nothing wants it and clear up after itself,
		// so a camera that starts asking again is not handed a position from
		// whenever the last one lost interest.
		bool VelocityInUse = false;
		bool TransformsStamped = false;
		// Builds opaque.vert paired with a surface shader's fragment stage.
		// Cached per shader and colour format, since the window and an
		// offscreen target do not share one.
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
		// Cache key: runtime code is keyed by identity and revision, a named
		// asset by its name, so the two never collide
		static std::string GetShaderCacheKey(ShaderScript *shader, const char *stageExtension);
		// Loads bytecode for `<source><extension>` from the shaders directory
		void *LoadShaderBytes(const std::string &source, const char *stageExtension, size_t &outSize);
		// Lays a script's parameters out for its shader, by name where the
		// layout is known and by slot order where it is not
		static std::vector<uint8_t> PackParameters(ShaderScript *shader, const CompiledShader &compiled);
	};
} // namespace gargantuan
