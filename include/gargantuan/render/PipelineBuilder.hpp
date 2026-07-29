#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <array>
#include <cstdint>
#include <string_view>

namespace gargantuan {
	struct PipelineBuilder {
	  public:
		// More than one only when a pass writes several pictures at once from
		// the same geometry, which is worth doing where the second one is
		// nearly free: drawing the scene again to get it would not be
		static constexpr size_t MAXIMUM_COLOR_TARGETS = 4;

		SDL_GPUShader *VertexShader = nullptr;
		SDL_GPUShader *FragmentShader = nullptr;

		SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		bool ColorEnabled = false;
		bool BlendingEnabled = false;

		SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		bool DepthEnabled = false;

		// Off for shaders that generate their own geometry from the vertex
		// index, like a fullscreen triangle, which bind no vertex buffer
		bool VertexInputEnabled = true;
		bool CullingEnabled = true;

		PipelineBuilder &SetVertexShader(SDL_GPUShader *shader);
		PipelineBuilder &SetFragmentShader(SDL_GPUShader *shader);
		PipelineBuilder &SetVertexInputEnabled(bool enabled);
		PipelineBuilder &SetCullingEnabled(bool enabled);
		PipelineBuilder &SetShaderFromCache(SDL_GPUDevice *gpu, std::string_view alias);
		PipelineBuilder &SetColorFormat(SDL_GPUTextureFormat format);
		// A further attachment, in the order the fragment shader's outputs are
		// laid out: SetColorFormat is location 0, the first added is location 1.
		// The shader has to write every one of them -- an attachment its
		// outputs do not cover is left undefined, not left alone.
		PipelineBuilder &AddColorFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetColorEnabled(bool enabled);
		PipelineBuilder &SetBlendingEnabled(bool enabled);
		PipelineBuilder &SetDepthFormat(SDL_GPUTextureFormat format);
		PipelineBuilder &SetDepthEnabled(bool enabled);

		SDL_GPUGraphicsPipelineCreateInfo BuildInfo();
		SDL_GPUGraphicsPipeline *Build(SDL_GPUDevice *gpu);

	  private:
		// ?????? Fuck you Sdl3?????????? Fuck you mean "Invalid blend factor enum!"
		//
		// Assertion failure at SDL_CreateGPUGraphicsPipeline_REAL (SDL_gpu.c:1062), triggered 1 time:
		//   '!"Invalid blend factor enum!"'
		//   Assertion failure at SDL_CreateGPUGraphicsPipeline_REAL (SDL_gpu.c:1062), triggered 1 time:
		//     '!"Invalid blend factor enum!"'
		//     Assertion failure at SDL_CreateGPUGraphicsPipeline_REAL (SDL_gpu.c:1062), triggered 1 time:
		//       '!"Invalid blend factor enum!"'
		std::array<SDL_GPUColorTargetDescription, MAXIMUM_COLOR_TARGETS> ColorTargets{};
		// Attachments past the first, which SetColorFormat owns
		size_t ExtraColorTargets = 0;
	};
} // namespace gargantuan
