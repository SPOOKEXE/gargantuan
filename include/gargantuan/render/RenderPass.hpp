#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>

namespace gargantuan {
	struct DrawContext {
		std::shared_ptr<WorldRoot> WorldRoot;
		std::shared_ptr<Camera> Camera;

		// Direction TOWARDS the light. Defaults to a fixed afternoon sun so a
		// draw without a Lighting service still shades sensibly.
		glm::vec3 LightDirection = glm::normalize(glm::vec3(0.75f, 1.0f, 0.5f));
	};

	struct FrameContext : DrawContext {
		SDL_GPUCommandBuffer *Commands;

		// Where the opaque pass draws. Either the window's swapchain texture
		// or an offscreen camera target, so passes must not assume a window.
		SDL_GPUTexture *ColorTarget;
		SDL_GPUTexture *DepthTexture;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler;
		glm::mat4 ShadowMatrix;

		uint32_t Width;
		uint32_t Height;
	};

	class RenderPass {
	  public:
		Shader Shader;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;

		virtual ~RenderPass() = default;
		virtual SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) = 0;
		virtual void Resize(SDL_GPUDevice *gpu, uint32_t width, uint32_t height) {};
		virtual void Destroy(SDL_GPUDevice *gpu);
	};
} // namespace gargantuan
