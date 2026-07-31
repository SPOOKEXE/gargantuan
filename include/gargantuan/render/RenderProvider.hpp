#pragma once

#include "gargantuan/render/RenderPass.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <memory>

namespace gargantuan {
	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);
	std::unique_ptr<RenderPass> CreateOverlayPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat);

	class RenderProvider {
	  public:
		RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu);

		RenderProvider(const RenderProvider &) = delete;
		RenderProvider &operator=(const RenderProvider &) = delete;

		void Draw(DrawContext drawContext);
		void Resize(int width, int height);
		void Destroy();

		SDL_Window *Window = nullptr;
		SDL_GPUDevice *Gpu = nullptr;
		SDL_GPUGraphicsPipeline *Pipeline = nullptr;
		SDL_GPUTexture *DepthTexture = nullptr;

		SDL_GPUTexture *ShadowMapTexture;
		SDL_GPUSampler *ShadowSampler = nullptr;

		SDL_GPUTextureFormat SwapchainFormat;

		std::unique_ptr<RenderPass> ShadowPass;
		std::unique_ptr<RenderPass> OpaquePass;
		std::unique_ptr<RenderPass> OverlayPass;
	};
} // namespace gargantuan
