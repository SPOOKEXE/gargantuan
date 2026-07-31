// The image layer composited over the swapchain after every pane has been
// blitted into it -- the debug panels ride on this.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace gargantuan {
	void RenderProvider::SetWindowOverlay(size_t overlayIndex, std::shared_ptr<EditableImage> image, glm::vec2 position) {
		if (overlayIndex >= MAXIMUM_WINDOW_OVERLAYS) {
			return;
		}
		WindowOverlays[overlayIndex] = {std::move(image), position};
	}

	// Built once. A failure here is a missing shader asset, which will not fix
	// itself, so WindowOverlayFailed latches rather than logging every frame.
	bool RenderProvider::EnsureWindowOverlayPipeline() {
		if (WindowOverlayPipeline) {
			return true;
		}

		WindowOverlayFailed = true;

		if (!FullscreenVertexShader) {
			FullscreenVertexShader = LoadBuiltinVertexShader("fullscreen", 0);
			if (!FullscreenVertexShader) {
				return false;
			}
		}

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, extension, entrypoint);

		size_t bytecodeByteCount = 0;
		void *shaderBytecode = LoadShaderBytes("window_overlay", ".frag", bytecodeByteCount);
		if (!shaderBytecode) {
			SDL_Log("Failed to load the window overlay shader");
			return false;
		}

		SDL_GPUShaderCreateInfo fragmentInfo{
			.code_size = bytecodeByteCount,
			.code = static_cast<const Uint8 *>(shaderBytecode),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
			.num_samplers = 1,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			.num_uniform_buffers = 1,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		SDL_free(shaderBytecode);

		if (!fragment) {
			SDL_Log("Failed to create the window overlay shader: %s", SDL_GetError());
			return false;
		}

		WindowOverlayPipeline = PipelineBuilder()
									.SetVertexShader(FullscreenVertexShader)
									.SetFragmentShader(fragment)
									.SetVertexInputEnabled(false)
									.SetCullingEnabled(false)
									.SetColorEnabled(true)
									.SetColorFormat(SwapchainFormat)
									// Composite over the existing window.
									.SetBlendingEnabled(true)
									.SetDepthEnabled(false)
									.Build(Gpu);
		SDL_ReleaseGPUShader(Gpu, fragment);

		if (!WindowOverlayPipeline) {
			SDL_Log("Failed to build the window overlay pipeline: %s", SDL_GetError());
			return false;
		}

		WindowOverlayFailed = false;
		return true;
	}

	void RenderProvider::RecordWindowOverlay(
		SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
	) {
		if (!commands || !target || width == 0 || height == 0 || WindowOverlayFailed) {
			return;
		}

		bool hasAnyOverlayImage = false;
		for (const auto &entry : WindowOverlays) {
			hasAnyOverlayImage = hasAnyOverlayImage || entry.Image != nullptr;
		}
		if (!hasAnyOverlayImage) {
			return;
		}

		if (!EnsureWindowOverlayPipeline()) {
			return;
		}

		// Point-sample pixel text to avoid smearing narrow glyphs.
		EnsurePointSampler();
		if (!PointSampler) {
			return;
		}

		struct alignas(16) OverlayUniforms {
			glm::vec4 TargetSizePixels;
			glm::vec4 RectPixels;
		};

		// Preserve the window picture beneath overlays.
		SDL_GPUColorTargetInfo colorTarget{
			.texture = target,
			.load_op = SDL_GPU_LOADOP_LOAD,
			.store_op = SDL_GPU_STOREOP_STORE,
		};

		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);
		SDL_BindGPUGraphicsPipeline(pass, WindowOverlayPipeline);

		// One pass for all panels; per-panel passes cost more than their triangles.
		for (const auto &entry : WindowOverlays) {
			if (!entry.Image) {
				continue;
			}

			SDL_GPUTexture *texture = GetOrUploadImageTexture(entry.Image.get());
			if (!texture) {
				continue;
			}

			OverlayUniforms uniforms{
				.TargetSizePixels = glm::vec4((float)width, (float)height, 0.0f, 0.0f),
				.RectPixels = glm::vec4(
					entry.Position.x,
					entry.Position.y,
					(float)entry.Image->GetWidth(),
					(float)entry.Image->GetHeight()
				),
			};

			SDL_GPUTextureSamplerBinding binding{.texture = texture, .sampler = PointSampler};
			SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
			SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(OverlayUniforms));
			SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		}

		SDL_EndGPURenderPass(pass);
	}

	void RenderProvider::ReleaseWindowOverlay() {
		if (WindowOverlayPipeline) {
			SDL_ReleaseGPUGraphicsPipeline(Gpu, WindowOverlayPipeline);
			WindowOverlayPipeline = nullptr;
		}
		WindowOverlays = {};
	}
}
