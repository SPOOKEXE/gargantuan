#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <unordered_map>

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

		// When a camera has a SurfaceShader, the opaque pass draws with this
		// pipeline instead of its own, and pushes the script's parameters as
		// the second fragment uniform buffer
		SDL_GPUGraphicsPipeline *SurfacePipeline = nullptr;
		const void *SurfaceParameters = nullptr;
		uint32_t SurfaceParameterBytes = 0;
		// Slot 0 is the shadow map; the script's images follow
		const SDL_GPUTextureSamplerBinding *SurfaceSamplers = nullptr;
		uint32_t SurfaceSamplerCount = 0;

		// Resolved once per frame: which texture, if any, each part shows on
		// its surface. Parts absent from the map use WhiteTexture.
		const std::unordered_map<const BasePart *, SDL_GPUTexture *> *PartTextures = nullptr;
		SDL_GPUTexture *WhiteTexture = nullptr;
		SDL_GPUSampler *SurfaceTextureSampler = nullptr;
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
