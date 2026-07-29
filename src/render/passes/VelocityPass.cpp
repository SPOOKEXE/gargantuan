#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>

namespace gargantuan {
	// Draws the scene again, writing where each pixel came from and how far
	// away it is. Only recorded for a camera whose chain asked for one of them.
	//
	// Both attachments always: the depth is the w a perspective projection has
	// already produced, so splitting them would mean drawing twice to save
	// writing one float.
	//
	// A separate pass rather than a second target on the opaque one, because a
	// SurfaceShader replaces that fragment stage entirely and would leave the
	// second attachment undefined.
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
						   // Both are measurements, not pictures: blending a
						   // transparent part's motion or distance into the one
						   // behind it would average two answers into a third
						   // that is neither
						   .SetBlendingEnabled(false)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			SDL_GPUColorTargetInfo colorTargets[2] = {
				// Zero is "did not move": an empty pixel reads the same place
				// in its history and the other tests decide
				{
					.texture = context.VelocityTarget,
					.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR,
					.store_op = SDL_GPU_STOREOP_STORE,
				},
				// The far plane is how far away nothing is. Zero would put the
				// background in front of everything.
				{
					.texture = context.ViewDepthTarget,
					.clear_color = SDL_FColor{Camera::FAR_PLANE, 0.0f, 0.0f, 0.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR,
					.store_op = SDL_GPU_STOREOP_STORE,
				},
			};

			// The opaque pass threw its depth away when it finished, so this
			// starts from a clear of its own rather than reusing what is there
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

			// Unjittered on both sides: the offset describes how the picture
			// was sampled, not where anything is
			glm::mat4 viewProjection = context.Camera->GetProjectionMatrix() * context.Camera->GetViewMatrix();
			WorldUniforms worldUniforms{
				.ViewProjection = viewProjection,
				// Before the first draw there is no previous frame; standing
				// still against itself reports no motion, which is what a
				// camera that has only ever existed at one place should say
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
