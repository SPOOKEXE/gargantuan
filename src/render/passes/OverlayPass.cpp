#include "gargantuan/DebugOverlay.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <cstring>
#include <memory>

namespace gargantuan {
	// The debug panels, composited over the finished frame. The panel itself is
	// drawn on the CPU into an OverlayImage; all this does is get those pixels
	// onto the swapchain.
	class OverlayPass final : public RenderPass {
	  public:
		struct alignas(16) OverlayUniforms {
			glm::vec4 Rect;
		};

		// Inset from the corner by the same margin the panel pads itself with
		static constexpr float MARGIN = 12.0f;

		FileShader Shader{
			.VertexFilepath = GetShaderPath("overlay.vert"),
			.VertexUniformBufferCount = 1,
			.FragmentFilepath = GetShaderPath("overlay.frag"),
			.FragmentUniformBufferCount = 0,
			.FragmentSamplerCount = 1,
		};

		OverlayPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
			Shader.Init(gpu);

			// Built by hand rather than with PipelineBuilder: that one binds the
			// mesh vertex layout and culls back faces, and this draws a quad it
			// generates from gl_VertexIndex with no vertex buffer at all.
			SDL_GPUColorTargetDescription colorTarget{};
			colorTarget.format = swapchainFormat;
			colorTarget.blend_state.enable_blend = true;
			colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
			colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
			colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

			SDL_GPUGraphicsPipelineCreateInfo info{};
			info.vertex_shader = Shader.VertexShader;
			info.fragment_shader = Shader.FragmentShader;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			// No depth target: the panel goes over whatever was drawn, and
			// testing it against the scene is the one thing it must not do.
			info.target_info.has_depth_stencil_target = false;

			Pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!Pipeline) {
				SDL_Log("Failed to create overlay pipeline: %s", SDL_GetError());
			}

			// Nearest: the font is a 3x5 bitmap scaled up, and filtering it
			// turns every glyph into a smudge.
			SDL_GPUSamplerCreateInfo samplerInfo{
				.min_filter = SDL_GPU_FILTER_NEAREST,
				.mag_filter = SDL_GPU_FILTER_NEAREST,
				.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
				.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
				.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
				.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			};
			Sampler = SDL_CreateGPUSampler(gpu, &samplerInfo);
		}

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			const OverlayImage &image = *context.Overlay;
			if (!Resize(gpu, image.GetWidth(), image.GetHeight())) {
				return nullptr;
			}

			void *mapped = SDL_MapGPUTransferBuffer(gpu, TransferBuffer, true);
			if (!mapped) {
				SDL_Log("Failed to map overlay transfer buffer: %s", SDL_GetError());
				return nullptr;
			}
			std::memcpy(mapped, image.GetPixels(), (size_t)TextureWidth * (size_t)TextureHeight * 4);
			SDL_UnmapGPUTransferBuffer(gpu, TransferBuffer);

			SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(context.Commands);
			SDL_GPUTextureTransferInfo source{.transfer_buffer = TransferBuffer, .offset = 0};
			SDL_GPUTextureRegion destination{
				.texture = Texture,
				.w = (uint32_t)TextureWidth,
				.h = (uint32_t)TextureHeight,
				.d = 1,
			};
			SDL_UploadToGPUTexture(copy, &source, &destination, true);
			SDL_EndGPUCopyPass(copy);

			// LOAD, not CLEAR: the frame underneath is the whole point
			SDL_GPUColorTargetInfo colorTarget{
				.texture = context.SwapchainTexture,
				.load_op = SDL_GPU_LOADOP_LOAD,
				.store_op = SDL_GPU_STOREOP_STORE,
			};

			auto pass = SDL_BeginGPURenderPass(context.Commands, &colorTarget, 1, nullptr);
			SDL_BindGPUGraphicsPipeline(pass, Pipeline);

			SDL_GPUTextureSamplerBinding binding{.texture = Texture, .sampler = Sampler};
			SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

			float viewportWidth = (float)std::max(context.Width, 1u);
			float viewportHeight = (float)std::max(context.Height, 1u);
			OverlayUniforms uniforms{
				.Rect = glm::vec4(
					MARGIN / viewportWidth,
					MARGIN / viewportHeight,
					(float)TextureWidth / viewportWidth,
					(float)TextureHeight / viewportHeight
				),
			};
			SDL_PushGPUVertexUniformData(context.Commands, 0, &uniforms, sizeof(OverlayUniforms));

			SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
			return pass;
		}

		void Destroy(SDL_GPUDevice *gpu) override {
			ReleaseTarget(gpu);

			if (Sampler) {
				SDL_ReleaseGPUSampler(gpu, Sampler);
				Sampler = nullptr;
			}

			RenderPass::Destroy(gpu);
			Shader.Destroy(gpu);
		}

	  private:
		// The panel changes size whenever a line is added or a number gets
		// wider, so the texture is rebuilt rather than sized once up front.
		bool Resize(SDL_GPUDevice *gpu, int width, int height) {
			if (width <= 0 || height <= 0) {
				return false;
			}
			if (Texture && width == TextureWidth && height == TextureHeight) {
				return true;
			}

			ReleaseTarget(gpu);

			SDL_GPUTextureCreateInfo textureInfo{
				.type = SDL_GPU_TEXTURETYPE_2D,
				.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
				.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
				.width = (uint32_t)width,
				.height = (uint32_t)height,
				.layer_count_or_depth = 1,
				.num_levels = 1,
			};
			Texture = SDL_CreateGPUTexture(gpu, &textureInfo);
			if (!Texture) {
				SDL_Log("Failed to create overlay texture: %s", SDL_GetError());
				return false;
			}

			SDL_GPUTransferBufferCreateInfo transferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = (uint32_t)(width * height * 4),
			};
			TransferBuffer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);
			if (!TransferBuffer) {
				SDL_Log("Failed to create overlay transfer buffer: %s", SDL_GetError());
				ReleaseTarget(gpu);
				return false;
			}

			TextureWidth = width;
			TextureHeight = height;
			return true;
		}

		void ReleaseTarget(SDL_GPUDevice *gpu) {
			if (TransferBuffer) {
				SDL_ReleaseGPUTransferBuffer(gpu, TransferBuffer);
				TransferBuffer = nullptr;
			}
			if (Texture) {
				SDL_ReleaseGPUTexture(gpu, Texture);
				Texture = nullptr;
			}
			TextureWidth = 0;
			TextureHeight = 0;
		}

		SDL_GPUTexture *Texture = nullptr;
		SDL_GPUTransferBuffer *TransferBuffer = nullptr;
		SDL_GPUSampler *Sampler = nullptr;
		int TextureWidth = 0;
		int TextureHeight = 0;
	};

	std::unique_ptr<RenderPass> CreateOverlayPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
		return std::make_unique<OverlayPass>(gpu, swapchainFormat);
	}
} // namespace gargantuan
