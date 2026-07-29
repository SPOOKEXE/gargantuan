#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <memory>

namespace gargantuan {
	// Draws the scene a second time, writing where each pixel came from rather
	// than what colour it is. Only recorded for a camera whose chain bound
	// Enum.RenderTexture.Velocity, so a place that never asks for motion
	// vectors never pays for the second pass.
	//
	// A separate pass rather than a second colour target on the opaque one: a
	// camera's SurfaceShader replaces the opaque fragment stage entirely, and a
	// pipeline with two attachments whose shader writes one leaves the other
	// undefined. Every user-written surface shader would have had to know about
	// motion vectors for this to be correct, which is not a bargain worth
	// striking for a buffer most cameras never want.
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
						   // Motion is a measurement, not a picture: blending a
						   // transparent part's velocity into the one behind it
						   // would average two answers into a third that is
						   // neither
						   .SetBlendingEnabled(false)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			// Zero is "did not move", which is what an empty pixel should say:
			// nothing was drawn there, so a temporal pass reads the same place
			// in its history and its own colour test decides the rest
			SDL_GPUColorTargetInfo colorTarget{
				.texture = context.VelocityTarget,
				.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f},
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
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

			SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(context.Commands, &colorTarget, 1, &depthTarget);
			SDL_BindGPUGraphicsPipeline(pass, Pipeline);

			// Unjittered on both sides. The sub-pixel offset describes how the
			// picture was sampled, not where anything is, and letting it in
			// would report a still object as drifting by however much the
			// offset moved between the two frames.
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
