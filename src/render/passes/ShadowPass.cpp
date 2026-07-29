#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <memory>
#include <vector>

namespace gargantuan {

	class ShadowPass final : public RenderPass {
	  public:
		struct alignas(16) Uniforms {
			glm::mat4 ShadowMatrix;
			glm::mat4 PartMatrix;
		};

		FileShader Shader{
			.VertexFilepath = GetShaderPath("shadow.vert"),
			.VertexUniformBufferCount = 1,
			.FragmentFilepath = GetShaderPath("shadow.frag"),
			.FragmentUniformBufferCount = 0,
		};

		ShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
			Shader.Init(gpu);

			Pipeline = PipelineBuilder()
						   .SetVertexShader(Shader.VertexShader)
						   .SetFragmentShader(Shader.FragmentShader)
						   .SetColorEnabled(false)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			glm::mat4 shadowProjection = glm::ortho<float>(
				-SHADOW_ORTHO_EXTENT, SHADOW_ORTHO_EXTENT, -SHADOW_ORTHO_EXTENT, SHADOW_ORTHO_EXTENT,
				SHADOW_ORTHO_NEAR, SHADOW_ORTHO_FAR
			);
			glm::vec3 lightPosition = glm::normalize(context.LightDirection) * SHADOW_EYE_DISTANCE;
			glm::mat4 shadowView = glm::lookAt(lightPosition, glm::vec3(0), glm::vec3(0, 1, 0));
			glm::mat4 shadowMatrix = shadowProjection * shadowView;
			context.ShadowMatrix = shadowMatrix;

			SDL_GPUDepthStencilTargetInfo depthTarget{
				.texture = context.ShadowMapTexture,
				.clear_depth = 1.0f,
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
				.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
				.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
			};

			SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(context.Commands, nullptr, 0, &depthTarget);
			SDL_BindGPUGraphicsPipeline(pass, Pipeline);

			// Includes offscreen casters whose shadows reach view; already filtered.
			std::vector<BasePart *> everything;
			PartSpan castList;
			if (context.Visible) {
				castList = context.Visible->ShadowParts();
			} else {
				everything.reserve(context.WorldRoot->RawParts.size());
				for (BasePart *candidate : context.WorldRoot->RawParts) {
					if (candidate->CastShadow) {
						everything.push_back(candidate);
					}
				}
				castList = {everything.data(), everything.size()};
			}

			SDL_GPUBuffer *boundVertexBuffer = nullptr;
			SDL_GPUBuffer *boundIndexBuffer = nullptr;

			for (BasePart *part : castList) {
				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				Uniforms uniforms{.ShadowMatrix = shadowMatrix, .PartMatrix = part->GetModelMatrix()};
				SDL_PushGPUVertexUniformData(context.Commands, 0, &uniforms, sizeof(Uniforms));

				if (mesh->VertexBuffer != boundVertexBuffer) {
					SDL_GPUBufferBinding vertexBinding{.buffer = mesh->VertexBuffer, .offset = 0};
					SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
					boundVertexBuffer = mesh->VertexBuffer;
				}

				if (mesh->IndexBuffer != boundIndexBuffer) {
					SDL_GPUBufferBinding indexBinding{.buffer = mesh->IndexBuffer, .offset = 0};
					SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
					boundIndexBuffer = mesh->IndexBuffer;
				}

				SDL_DrawGPUIndexedPrimitives(pass, mesh->IndexCount, 1, 0, 0, 0);
			}

			return pass;
		};
	};

	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
		return std::make_unique<ShadowPass>(gpu, swapchainFormat);
	}

} // namespace gargantuan
