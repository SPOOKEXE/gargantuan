#pragma once

#include "gargantuan/render/RenderPass.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <lua.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	class Camera;
	class EditableImage;
	class ThreadEngine;

	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);

	class RenderProvider {
	  public:
		// Offscreen cameras render at a fixed format so one extra pipeline
		// covers them all, whatever the window's swapchain happens to be
		static constexpr SDL_GPUTextureFormat OFFSCREEN_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		// The colour and depth textures backing one offscreen camera
		struct CameraTarget {
			SDL_GPUTexture *ColorTexture = nullptr;
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

		std::unordered_map<Camera *, CameraTarget> CameraTargets;
		std::vector<PendingRender> PendingRenders;

		// Returns the camera's target, sized to its ViewportSize, or nullptr
		// when the viewport is empty
		CameraTarget *AcquireCameraTarget(Camera *camera);
		// Records the shadow and opaque passes for one camera into `commands`
		bool RecordCameraPasses(
			SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
		);
	};
} // namespace gargantuan
