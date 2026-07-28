#pragma once

#include "gargantuan/render/RenderPass.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <lua.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	class Camera;
	class ComputeShader;
	class EditableImage;
	class PostProcessShader;
	class ShaderScript;
	class ThreadEngine;

	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);

	class RenderProvider {
	  public:
		// Offscreen cameras render at a fixed format so one extra pipeline
		// covers them all, whatever the window's swapchain happens to be
		static constexpr SDL_GPUTextureFormat OFFSCREEN_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		// The colour and depth textures backing one offscreen camera.
		// ScratchTexture is the other half of the ping-pong pair the shader
		// chain bounces through, and is only created once a camera has shaders.
		struct CameraTarget {
			SDL_GPUTexture *ColorTexture = nullptr;
			SDL_GPUTexture *ScratchTexture = nullptr;
			SDL_GPUTexture *DepthTexture = nullptr;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		// Draws a camera to the window
		void Draw(DrawContext drawContext);
		// Draws a camera into its own offscreen target, creating or resizing
		// that target to match the camera's ViewportSize first
		void DrawOffscreen(DrawContext drawContext);

		// Renders the camera offscreen, starts a download of the result, and
		// parks `thread` until it lands. The thread is resumed with an
		// EditableImage. Returns false if the render could not be started.
		bool RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine);
		// Resumes any threads whose downloads have finished
		void PollRenders(ThreadEngine *threadEngine);

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

		SDL_Window *Window = nullptr;
		SDL_GPUDevice *Gpu = nullptr;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;
		SDL_GPUTexture *DepthTexture = nullptr;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler = nullptr;

		SDL_GPUTextureFormat SwapchainFormat;

		std::unique_ptr<RenderPass> ShadowPass;
		std::unique_ptr<RenderPass> OpaquePass;
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
			// Set once a compile has been attempted and failed, so the engine
			// complains once rather than every frame
			bool Failed = false;
		};

		// What the shaders are handed alongside their own parameters
		struct alignas(16) BuiltinUniforms {
			glm::vec4 Resolution;
			glm::vec4 Time;
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
		std::unordered_map<std::string, CompiledShader> ShaderCache;
		SDL_GPUShader *FullscreenVertexShader = nullptr;
		SDL_GPUSampler *ShaderSampler = nullptr;

		// Returns the camera's target, sized to its ViewportSize, or nullptr
		// when the viewport is empty. `withScratch` also guarantees the second
		// ping-pong texture exists.
		CameraTarget *AcquireCameraTarget(Camera *camera, bool withScratch);
		// Records the shadow and opaque passes for one camera into `commands`
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
		);
		// Runs the camera's shader chain, leaving the result in ColorTexture
		void RecordShaderChain(SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target);

		// Both prefer the script's runtime-compiled bytecode and fall back to
		// its named build-time asset
		// Uploads or refreshes the GPU copy of an image, returning null when it
		// is empty or the upload failed
		SDL_GPUTexture *AcquireImageTexture(EditableImage *image);
		CompiledShader *GetPostProcessShader(PostProcessShader *shader);
		CompiledShader *GetComputeShader(ComputeShader *shader);
		// Cache key: runtime code is keyed by identity and revision, a named
		// asset by its name, so the two never collide
		static std::string GetShaderCacheKey(ShaderScript *shader, const char *stageExtension);
		// Loads bytecode for `<source><extension>` from the shaders directory
		void *LoadShaderBytes(const std::string &source, const char *stageExtension, size_t &outSize);
	};
} // namespace gargantuan
