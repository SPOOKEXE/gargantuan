#pragma once

#include "gargantuan/render/Mesh.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <array>
#include <cstdint>
#include <string_view>

namespace gargantuan {
	struct PipelineBuilder {
	  public:
		static constexpr size_t MAXIMUM_COLOR_TARGETS = 4;

		SDL_GPUShader *VertexShader = nullptr;
		SDL_GPUShader *FragmentShader = nullptr;

		SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		bool ColorEnabled = false;
		bool BlendingEnabled = false;

		SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		bool DepthEnabled = false;

		bool VertexInputEnabled = true;
		bool CullingEnabled = true;

		VertexStreams EnabledVertexStreams = VertexStreams::All;

		PipelineBuilder &SetVertexShader(SDL_GPUShader *shader);
		PipelineBuilder &SetFragmentShader(SDL_GPUShader *shader);
		PipelineBuilder &SetVertexInputEnabled(bool enabled);
		PipelineBuilder &SetVertexStreams(VertexStreams streams);
		PipelineBuilder &SetCullingEnabled(bool enabled);
		PipelineBuilder &SetColorFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &AddColorFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetColorEnabled(bool enabled);
		PipelineBuilder &SetBlendingEnabled(bool enabled);
		PipelineBuilder &SetDepthFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetDepthEnabled(bool enabled);

		SDL_GPUGraphicsPipelineCreateInfo BuildCreateInfo();
		SDL_GPUGraphicsPipeline *Build(SDL_GPUDevice *gpu);

	  private:
		std::array<SDL_GPUColorTargetDescription, MAXIMUM_COLOR_TARGETS> ColorTargetDescriptions{};
		size_t ExtraColorTargetCount = 0;
		Vertex::Layout SplitStreamVertexLayout;
	};
}
