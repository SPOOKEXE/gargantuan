#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>

namespace gargantuan {
	// Writes velocity and view depth together when requested. Kept separate
	// because SurfaceShader replaces the opaque fragment stage.
	class VelocityPass final : public RenderPass {
	  public:
		struct alignas(16) WorldUniforms {
			glm::mat4 ViewProjection;
			glm::mat4 PreviousViewProjection;
		};

		struct alignas(16) PartUniforms {
			glm::mat4 ModelMatrix;
			glm::mat4 PreviousModelMatrix;
		};

		FileShader Shader{
			.VertexFilepath = GetShaderPath("velocity.vert"),
			.VertexUniformBufferCount = 2,
			.FragmentFilepath = GetShaderPath("velocity.frag"),
			.FragmentUniformBufferCount = 0,
		};

		VelocityPass(SDL_GPUDevice *gpu) {
			Shader.Init(gpu);
			Pipeline = PipelineBuilder()
						   .SetVertexShader(Shader.VertexShader)
						   .SetFragmentShader(Shader.FragmentShader)
						   .SetColorEnabled(true)
						   .SetColorFormat(RenderProvider::VELOCITY_FORMAT)
						   .AddColorFormat(RenderProvider::VIEW_DEPTH_FORMAT)
						   // Blending would invent motion and depth values.
						   .SetBlendingEnabled(false)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			SDL_GPUColorTargetInfo colorTargets[2] = {
				// Empty pixels report no motion.
				{
					.texture = context.VelocityTarget,
					.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR,
					.store_op = SDL_GPU_STOREOP_STORE,
				},
				// Empty pixels lie at the far plane.
				{
					.texture = context.ViewDepthTarget,
					.clear_color = SDL_FColor{Camera::FAR_PLANE, 0.0f, 0.0f, 0.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR,
					.store_op = SDL_GPU_STOREOP_STORE,
				},
			};

			// Opaque discards depth, so this pass clears its own.
			SDL_GPUDepthStencilTargetInfo depthTarget{
				.texture = context.DepthTexture,
				.clear_depth = 1.0f,
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_DONT_CARE,
				.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
				.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
			};

			SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(context.Commands, colorTargets, 2, &depthTarget);
			SDL_BindGPUGraphicsPipeline(pass, Pipeline);

			// Motion uses unjittered matrices; jitter changes sampling, not position.
			glm::mat4 viewProjection = context.Camera->GetProjectionMatrix() * context.Camera->GetViewMatrix();
			WorldUniforms worldUniforms{
				.ViewProjection = viewProjection,
				// First draw compares against itself and reports no motion.
				.PreviousViewProjection =
					context.Camera->HasPreviousViewProjection ? context.Camera->PreviousViewProjection : viewProjection,
			};
			SDL_PushGPUVertexUniformData(context.Commands, 0, &worldUniforms, sizeof(WorldUniforms));

			for (auto part : context.WorldRoot->Parts) {
				if (context.Visible && !context.Visible->IsInView(part.get())) {
					continue;
				}

				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				glm::mat4 model = part->GetModelMatrix();
				PartUniforms uniforms{
					.ModelMatrix = model,
					.PreviousModelMatrix = part->HasPreviousModelMatrix ? part->PreviousModelMatrix : model,
				};
				SDL_PushGPUVertexUniformData(context.Commands, 1, &uniforms, sizeof(PartUniforms));

				SDL_GPUBufferBinding vertexBinding{.buffer = mesh->VertexBuffer, .offset = 0};
				SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

				SDL_GPUBufferBinding indexBinding{.buffer = mesh->IndexBuffer, .offset = 0};
				SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

				SDL_DrawGPUIndexedPrimitives(pass, mesh->IndexCount, 1, 0, 0, 0);
			}

			return pass;
		};
	};

	std::unique_ptr<RenderPass> CreateVelocityPass(SDL_GPUDevice *gpu) {
		return std::make_unique<VelocityPass>(gpu);
	}
} // namespace gargantuan
