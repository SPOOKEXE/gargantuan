#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <array>
#include <cstdint>
#include <string_view>

namespace gargantuan {
	struct PipelineBuilder {
	  public:
		// Extra outputs share one geometry pass.
		static constexpr size_t MAXIMUM_COLOR_TARGETS = 4;

		SDL_GPUShader *VertexShader = nullptr;
		SDL_GPUShader *FragmentShader = nullptr;

		SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		bool ColorEnabled = false;
		bool BlendingEnabled = false;

		SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		bool DepthEnabled = false;

		// Disable for gl_VertexIndex geometry with no vertex buffer.
		bool VertexInputEnabled = true;
		bool CullingEnabled = true;

		PipelineBuilder &SetVertexShader(SDL_GPUShader *shader);
		PipelineBuilder &SetFragmentShader(SDL_GPUShader *shader);
		PipelineBuilder &SetVertexInputEnabled(bool enabled);
		PipelineBuilder &SetCullingEnabled(bool enabled);
		PipelineBuilder &SetShaderFromCache(SDL_GPUDevice *gpu, std::string_view alias);
		PipelineBuilder &SetColorFormat(SDL_GPUTextureFormat format);
		// Adds the next fragment-output location after SetColorFormat's location 0.
		// The shader must write every declared attachment.
		PipelineBuilder &AddColorFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetColorEnabled(bool enabled);
		PipelineBuilder &SetBlendingEnabled(bool enabled);
		PipelineBuilder &SetDepthFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetDepthEnabled(bool enabled);

		SDL_GPUGraphicsPipelineCreateInfo BuildInfo();
		SDL_GPUGraphicsPipeline *Build(SDL_GPUDevice *gpu);

	  private:
		std::array<SDL_GPUColorTargetDescription, MAXIMUM_COLOR_TARGETS> ColorTargets{};
		// Count after location 0, owned by SetColorFormat.
		size_t ExtraColorTargets = 0;
	};
} // namespace gargantuan
