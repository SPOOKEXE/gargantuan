// #define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace gargantuan {
	static RenderProvider *CURRENT_PROVIDER = nullptr;

	RenderProvider *RenderProvider::GetCurrent() {
		return CURRENT_PROVIDER;
	}

	void RenderProvider::SetCurrent(RenderProvider *provider) {
		CURRENT_PROVIDER = provider;
	}

	RenderProvider::RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu) : Window(window), Gpu(gpu) {
		if (!SDL_ClaimWindowForGPUDevice(Gpu, Window)) {
			SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
			std::abort();
		}

		SwapchainFormat = SDL_GetGPUSwapchainTextureFormat(Gpu, Window);

		SDL_GPUTextureCreateInfo info{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
			.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
			.width = 2048,
			.height = 2048,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		ShadowMapTexture = SDL_CreateGPUTexture(gpu, &info);

		SDL_GPUSamplerCreateInfo samplerInfo{
			.min_filter = SDL_GPU_FILTER_LINEAR,
			.mag_filter = SDL_GPU_FILTER_LINEAR,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
			.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
			.enable_compare = true,
		};
		ShadowSampler = SDL_CreateGPUSampler(gpu, &samplerInfo);

		int width, height;
		SDL_GetWindowSizeInPixels(Window, &width, &height);
		Resize(width, height);

		SDL_Log("Creating shadow pass");
		ShadowPass = CreateShadowPass(Gpu, SwapchainFormat);

		SDL_Log("Creating opaque pass");
		OpaquePass = CreateOpaquePass(Gpu, SwapchainFormat);

		SDL_Log("Creating offscreen opaque pass");
		OffscreenOpaquePass = CreateOpaquePass(Gpu, OFFSCREEN_FORMAT);
	}

	void RenderProvider::Destroy() {
		SDL_WaitForGPUIdle(Gpu);

		// Anything still waiting on a fence will never be resumed now
		for (auto &pending : PendingRenders) {
			if (pending.Fence) SDL_ReleaseGPUFence(Gpu, pending.Fence);
			if (pending.TransferBuffer) SDL_ReleaseGPUTransferBuffer(Gpu, pending.TransferBuffer);
		}
		PendingRenders.clear();

		for (auto &[_, target] : CameraTargets) {
			if (target.ColorTexture) SDL_ReleaseGPUTexture(Gpu, target.ColorTexture);
			if (target.ScratchTexture) SDL_ReleaseGPUTexture(Gpu, target.ScratchTexture);
			if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);
		}
		CameraTargets.clear();

		for (auto &[_, uploaded] : UploadedImages) {
			if (uploaded.Texture) SDL_ReleaseGPUTexture(Gpu, uploaded.Texture);
		}
		UploadedImages.clear();

		for (auto &[_, compiled] : ShaderCache) {
			if (compiled.GraphicsPipeline) SDL_ReleaseGPUGraphicsPipeline(Gpu, compiled.GraphicsPipeline);
			if (compiled.ComputePipeline) SDL_ReleaseGPUComputePipeline(Gpu, compiled.ComputePipeline);
		}
		ShaderCache.clear();

		if (FullscreenVertexShader) {
			SDL_ReleaseGPUShader(Gpu, FullscreenVertexShader);
			FullscreenVertexShader = nullptr;
		}

		if (ShaderSampler) {
			SDL_ReleaseGPUSampler(Gpu, ShaderSampler);
			ShaderSampler = nullptr;
		}

		if (DepthTexture != nullptr) {
			SDL_ReleaseGPUTexture(Gpu, DepthTexture);
			DepthTexture = nullptr;
		};

		if (ShadowMapTexture) {
			SDL_ReleaseGPUTexture(Gpu, ShadowMapTexture);
			ShadowMapTexture = nullptr;
		}

		if (ShadowSampler) {
			SDL_ReleaseGPUSampler(Gpu, ShadowSampler);
			ShadowSampler = nullptr;
		}

		ShadowPass->Destroy(Gpu);
		OpaquePass->Destroy(Gpu);
		OffscreenOpaquePass->Destroy(Gpu);

		if (CURRENT_PROVIDER == this) {
			CURRENT_PROVIDER = nullptr;
		}
	}

	void RenderProvider::ReleaseCameraTarget(Camera *camera) {
		auto it = CameraTargets.find(camera);
		if (it == CameraTargets.end()) {
			return;
		}

		if (it->second.ColorTexture) SDL_ReleaseGPUTexture(Gpu, it->second.ColorTexture);
		if (it->second.ScratchTexture) SDL_ReleaseGPUTexture(Gpu, it->second.ScratchTexture);
		if (it->second.DepthTexture) SDL_ReleaseGPUTexture(Gpu, it->second.DepthTexture);
		CameraTargets.erase(it);
	}

	// Camera targets double as shader inputs and outputs, so they need every
	// usage the chain might ask of them
	static constexpr SDL_GPUTextureUsageFlags CAMERA_TARGET_USAGE =
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER |
		SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

	RenderProvider::CameraTarget *RenderProvider::AcquireCameraTarget(Camera *camera, bool withScratch) {
		if (!camera) {
			return nullptr;
		}

		auto size = camera->ViewportSize;
		uint32_t width = (uint32_t)glm::max(size.GetX(), 0.0f);
		uint32_t height = (uint32_t)glm::max(size.GetY(), 0.0f);
		if (width == 0 || height == 0) {
			return nullptr;
		}

		CameraTarget &target = CameraTargets[camera];
		bool sized = target.ColorTexture != nullptr && target.Width == width && target.Height == height;
		bool scratchReady = !withScratch || target.ScratchTexture != nullptr;
		if (sized && scratchReady) {
			return &target;
		}

		SDL_GPUTextureCreateInfo colorInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = OFFSCREEN_FORMAT,
			.usage = CAMERA_TARGET_USAGE,
			.width = width,
			.height = height,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};

		// The scratch half is only paid for once a camera actually has shaders
		if (sized) {
			target.ScratchTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
			if (!target.ScratchTexture) {
				SDL_Log("Failed to create a %ux%u shader scratch target: %s", width, height, SDL_GetError());
			}
			return &target;
		}

		// The viewport changed, so the old textures are the wrong size
		if (target.ColorTexture) SDL_ReleaseGPUTexture(Gpu, target.ColorTexture);
		if (target.ScratchTexture) SDL_ReleaseGPUTexture(Gpu, target.ScratchTexture);
		if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);
		target.ScratchTexture = nullptr;

		target.ColorTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
		if (withScratch) {
			target.ScratchTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
		}

		// Must match the depth format the opaque pipeline was built with
		SDL_GPUTextureCreateInfo depthInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
			.width = width,
			.height = height,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		target.DepthTexture = SDL_CreateGPUTexture(Gpu, &depthInfo);

		target.Width = width;
		target.Height = height;

		if (!target.ColorTexture || !target.DepthTexture) {
			SDL_Log("Failed to create a %ux%u camera target: %s", width, height, SDL_GetError());
			ReleaseCameraTarget(camera);
			return nullptr;
		}

		return &target;
	}

	SDL_GPUTexture *RenderProvider::AcquireImageTexture(EditableImage *image) {
		if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
			return nullptr;
		}

		uint32_t width = (uint32_t)image->GetWidth();
		uint32_t height = (uint32_t)image->GetHeight();

		UploadedImage &uploaded = UploadedImages[image];
		bool sized = uploaded.Texture != nullptr && uploaded.Width == width && uploaded.Height == height;
		if (sized && uploaded.Revision == image->GetRevision()) {
			return uploaded.Texture;
		}

		if (!sized) {
			if (uploaded.Texture) SDL_ReleaseGPUTexture(Gpu, uploaded.Texture);

			SDL_GPUTextureCreateInfo info{
				.type = SDL_GPU_TEXTURETYPE_2D,
				.format = OFFSCREEN_FORMAT,
				.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
				.width = width,
				.height = height,
				.layer_count_or_depth = 1,
				.num_levels = 1,
			};
			uploaded.Texture = SDL_CreateGPUTexture(Gpu, &info);
			uploaded.Width = width;
			uploaded.Height = height;
		}

		if (!uploaded.Texture) {
			return nullptr;
		}

		uint32_t bytes = width * height * EditableImage::CHANNELS;
		SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = bytes,
		};
		SDL_GPUTransferBuffer *transferBuffer = SDL_CreateGPUTransferBuffer(Gpu, &transferInfo);
		if (!transferBuffer) {
			return nullptr;
		}

		if (void *mapped = SDL_MapGPUTransferBuffer(Gpu, transferBuffer, false)) {
			std::memcpy(mapped, image->Pixels.data(), bytes);
			SDL_UnmapGPUTransferBuffer(Gpu, transferBuffer);
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commands);
		SDL_GPUTextureTransferInfo source{
			.transfer_buffer = transferBuffer,
			.offset = 0,
			.pixels_per_row = width,
			.rows_per_layer = height,
		};
		SDL_GPUTextureRegion destination{.texture = uploaded.Texture, .w = width, .h = height, .d = 1};
		SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
		SDL_EndGPUCopyPass(copyPass);
		SDL_SubmitGPUCommandBuffer(commands);
		SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);

		uploaded.Revision = image->GetRevision();
		return uploaded.Texture;
	}

	void *RenderProvider::LoadShaderBytes(const std::string &source, const char *stageExtension, size_t &outSize) {
		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string bytecodeExtension, entrypoint;
		GetShaderFormat(Gpu, format, bytecodeExtension, entrypoint);

		auto path = GetShaderPath(source + stageExtension).string() + bytecodeExtension;
		void *code = SDL_LoadFile(path.c_str(), &outSize);
		if (!code) {
			SDL_Log("Shader '%s' not found at %s", source.c_str(), path.c_str());
		}
		return code;
	}

	// Runtime code is keyed by identity and revision so editing it rebuilds the
	// pipeline; a named asset is keyed by its name so cameras share one
	std::string RenderProvider::GetShaderCacheKey(ShaderScript *shader, const char *stageExtension) {
		if (shader->HasBytecode()) {
			return "code:" + std::to_string((uintptr_t)shader) + ":" + std::to_string(shader->GetRevision());
		}
		return shader->Source + stageExtension;
	}

	RenderProvider::CompiledShader *RenderProvider::GetPostProcessShader(PostProcessShader *shader) {
		// A shader that samples an extra image declares two samplers, so the
		// pipeline for it is a different one and needs its own cache entry
		uint32_t samplerCount = shader->GetImage() ? 2 : 1;
		std::string key = GetShaderCacheKey(shader, ".frag") + "#s" + std::to_string(samplerCount);

		auto it = ShaderCache.find(key);
		if (it != ShaderCache.end()) {
			return it->second.Failed ? nullptr : &it->second;
		}

		CompiledShader &compiled = ShaderCache[key];
		compiled.Failed = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetShaderFormat(Gpu, format, extension, entrypoint);

		// Every post-process shader shares the one fullscreen vertex stage
		if (!FullscreenVertexShader) {
			size_t size = 0;
			void *code = LoadShaderBytes("fullscreen", ".vert", size);
			if (!code) {
				return nullptr;
			}

			SDL_GPUShaderCreateInfo info{
				.code_size = size,
				.code = static_cast<const Uint8 *>(code),
				.entrypoint = entrypoint.c_str(),
				.format = format,
				.stage = SDL_GPU_SHADERSTAGE_VERTEX,
			};
			FullscreenVertexShader = SDL_CreateGPUShader(Gpu, &info);
			SDL_free(code);

			if (!FullscreenVertexShader) {
				SDL_Log("Failed to create the fullscreen vertex shader: %s", SDL_GetError());
				return nullptr;
			}
		}

		// Runtime bytecode wins; otherwise fall back to the named build asset
		size_t size = shader->HasBytecode() ? shader->GetBytecode().size() : 0;
		void *code = nullptr;
		if (shader->HasBytecode()) {
			code = SDL_malloc(size);
			std::memcpy(code, shader->GetBytecode().data(), size);
		} else {
			code = LoadShaderBytes(shader->Source, ".frag", size);
		}
		if (!code) {
			return nullptr;
		}

		SDL_GPUShaderCreateInfo fragmentInfo{
			.code_size = size,
			.code = static_cast<const Uint8 *>(code),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
			.num_samplers = samplerCount,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			// slot 0 builtins, slot 1 the script's own parameters
			.num_uniform_buffers = 2,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		SDL_free(code);

		if (!fragment) {
			SDL_Log("Failed to create fragment shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		// The fullscreen triangle comes out of gl_VertexIndex, so no vertex
		// buffer is bound and its winding must not be culled
		compiled.GraphicsPipeline = PipelineBuilder()
										.SetVertexShader(FullscreenVertexShader)
										.SetFragmentShader(fragment)
										.SetVertexInputEnabled(false)
										.SetCullingEnabled(false)
										.SetColorEnabled(true)
										.SetColorFormat(OFFSCREEN_FORMAT)
										.SetDepthEnabled(false)
										.Build(Gpu);

		// The pipeline holds its own reference to the shader module
		SDL_ReleaseGPUShader(Gpu, fragment);

		if (!compiled.GraphicsPipeline) {
			SDL_Log("Failed to build a pipeline for shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.Failed = false;
		return &compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetComputeShader(ComputeShader *shader) {
		std::string key = GetShaderCacheKey(shader, ".comp");

		auto it = ShaderCache.find(key);
		if (it != ShaderCache.end()) {
			return it->second.Failed ? nullptr : &it->second;
		}

		CompiledShader &compiled = ShaderCache[key];
		compiled.Failed = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetShaderFormat(Gpu, format, extension, entrypoint);

		size_t size = shader->HasBytecode() ? shader->GetBytecode().size() : 0;
		void *code = nullptr;
		if (shader->HasBytecode()) {
			code = SDL_malloc(size);
			std::memcpy(code, shader->GetBytecode().data(), size);
		} else {
			code = LoadShaderBytes(shader->Source, ".comp", size);
		}
		if (!code) {
			return nullptr;
		}

		glm::vec3 threadGroupSize = shader->ThreadGroupSize;
		SDL_GPUComputePipelineCreateInfo info{
			.code_size = size,
			.code = static_cast<const Uint8 *>(code),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.num_samplers = 0,
			.num_readonly_storage_textures = 1,
			.num_readonly_storage_buffers = 0,
			.num_readwrite_storage_textures = 1,
			.num_readwrite_storage_buffers = 0,
			.num_uniform_buffers = 2,
			.threadcount_x = (uint32_t)glm::max(threadGroupSize.x, 1.0f),
			.threadcount_y = (uint32_t)glm::max(threadGroupSize.y, 1.0f),
			.threadcount_z = (uint32_t)glm::max(threadGroupSize.z, 1.0f),
		};
		compiled.ComputePipeline = SDL_CreateGPUComputePipeline(Gpu, &info);
		SDL_free(code);

		if (!compiled.ComputePipeline) {
			SDL_Log("Failed to create compute shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.Failed = false;
		return &compiled;
	}

	void RenderProvider::RecordShaderChain(
		SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target
	) {
		if (!camera || camera->Shaders.empty() || !target.ScratchTexture) {
			return;
		}

		if (!ShaderSampler) {
			SDL_GPUSamplerCreateInfo samplerInfo{
				.min_filter = SDL_GPU_FILTER_LINEAR,
				.mag_filter = SDL_GPU_FILTER_LINEAR,
				.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
				.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
				.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
				.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			};
			ShaderSampler = SDL_CreateGPUSampler(Gpu, &samplerInfo);
			if (!ShaderSampler) {
				return;
			}
		}

		BuiltinUniforms builtin{
			.Resolution = glm::vec4(target.Width, target.Height, 1.0f / target.Width, 1.0f / target.Height),
			.Time = glm::vec4((float)Scene.Time, 0.0f, 0.0f, 0.0f),
		};

		// The chain bounces between the two textures; `source` always holds
		// what has been produced so far
		SDL_GPUTexture *source = target.ColorTexture;
		SDL_GPUTexture *destination = target.ScratchTexture;

		for (auto &shader : camera->Shaders) {
			// A script with neither compiled code nor an asset name has nothing to run
			if (!shader || (shader->Source.empty() && !shader->HasBytecode())) {
				continue;
			}

			auto parameters = shader->GetPackedParameters();
			// SDL rejects a zero-length uniform push, so always send one slot
			if (parameters.empty()) {
				parameters.push_back(glm::vec4(0.0f));
			}
			uint32_t parameterBytes = (uint32_t)(parameters.size() * sizeof(glm::vec4));

			if (auto *post = shader->Cast<PostProcessShader>()) {
				CompiledShader *compiled = GetPostProcessShader(post);
				if (!compiled) {
					continue;
				}

				SDL_GPUColorTargetInfo colorTarget{
					.texture = destination,
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.store_op = SDL_GPU_STOREOP_STORE,
				};
				SDL_GPUTextureSamplerBinding bindings[2] = {
					{.texture = source, .sampler = ShaderSampler},
					{.texture = nullptr, .sampler = ShaderSampler},
				};
				uint32_t bindingCount = 1;
				if (auto image = post->GetImage()) {
					if (auto *texture = AcquireImageTexture(image.get())) {
						bindings[1].texture = texture;
						bindingCount = 2;
					}
				}

				SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);
				SDL_BindGPUGraphicsPipeline(pass, compiled->GraphicsPipeline);
				SDL_BindGPUFragmentSamplers(pass, 0, bindings, bindingCount);
				SDL_PushGPUFragmentUniformData(commands, 0, &builtin, sizeof(BuiltinUniforms));
				SDL_PushGPUFragmentUniformData(commands, 1, parameters.data(), parameterBytes);
				// The fullscreen triangle needs no vertex buffer
				SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
				SDL_EndGPURenderPass(pass);
			} else if (auto *compute = shader->Cast<ComputeShader>()) {
				CompiledShader *compiled = GetComputeShader(compute);
				if (!compiled) {
					continue;
				}

				SDL_GPUStorageTextureReadWriteBinding writeBinding{.texture = destination, .cycle = false};
				SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(commands, &writeBinding, 1, nullptr, 0);
				SDL_BindGPUComputePipeline(pass, compiled->ComputePipeline);
				SDL_BindGPUComputeStorageTextures(pass, 0, &source, 1);
				SDL_PushGPUComputeUniformData(commands, 0, &builtin, sizeof(BuiltinUniforms));
				SDL_PushGPUComputeUniformData(commands, 1, parameters.data(), parameterBytes);

				// Round up so the edge groups still cover the last pixels; the
				// shader discards the invocations that land outside
				uint32_t groupX = (uint32_t)glm::max(compute->ThreadGroupSize.x, 1.0f);
				uint32_t groupY = (uint32_t)glm::max(compute->ThreadGroupSize.y, 1.0f);
				SDL_DispatchGPUCompute(
					pass, (target.Width + groupX - 1) / groupX, (target.Height + groupY - 1) / groupY, 1
				);
				SDL_EndGPUComputePass(pass);
			} else {
				// A bare ShaderScript has no stage to run
				continue;
			}

			std::swap(source, destination);
		}

		// Whatever ran last, the camera's own texture has to end up holding the
		// result so readback and sampling stay simple
		if (source != target.ColorTexture) {
			SDL_GPUBlitInfo blit{
				.source = {.texture = source, .w = target.Width, .h = target.Height},
				.destination = {.texture = target.ColorTexture, .w = target.Width, .h = target.Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &blit);
		}
	}

	bool RenderProvider::RecordCameraPasses(
		SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTarget &target
	) {
		if (!commands || !target.ColorTexture || !target.DepthTexture || !ShadowMapTexture) {
			return false;
		}

		FrameContext frameContext;
		frameContext.Commands = commands;
		frameContext.WorldRoot = drawContext.WorldRoot;
		frameContext.Camera = drawContext.Camera;
		frameContext.LightDirection = glm::normalize(drawContext.LightDirection);
		frameContext.ShadowMapTexture = ShadowMapTexture;
		frameContext.ShadowSampler = ShadowSampler;
		frameContext.ColorTarget = target.ColorTexture;
		frameContext.DepthTexture = target.DepthTexture;
		frameContext.Width = target.Width;
		frameContext.Height = target.Height;

		// The shadow map only depends on the light, but it has to be recorded
		// into this command buffer for the opaque pass to sample it
		SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));
		SDL_EndGPURenderPass(OffscreenOpaquePass->Draw(Gpu, frameContext));
		return true;
	}

	void RenderProvider::DrawOffscreen(DrawContext drawContext) {
		auto *camera = drawContext.Camera.get();
		CameraTarget *target = AcquireCameraTarget(camera, camera && !camera->Shaders.empty());
		if (!target) {
			return;
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		if (!RecordCameraPasses(commands, drawContext, *target)) {
			SDL_CancelGPUCommandBuffer(commands);
			return;
		}

		RecordShaderChain(commands, camera, *target);
		SDL_SubmitGPUCommandBuffer(commands);
	}

	bool RenderProvider::RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine) {
		auto *camera = drawContext.Camera.get();
		CameraTarget *target = AcquireCameraTarget(camera, camera && !camera->Shaders.empty());
		if (!target || !threadEngine) {
			return false;
		}

		uint32_t width = target->Width;
		uint32_t height = target->Height;
		uint32_t bytes = width * height * EditableImage::CHANNELS;

		SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
			.size = bytes,
		};
		SDL_GPUTransferBuffer *transferBuffer = SDL_CreateGPUTransferBuffer(Gpu, &transferInfo);
		if (!transferBuffer) {
			SDL_Log("Failed to create a %u byte readback buffer: %s", bytes, SDL_GetError());
			return false;
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
			return false;
		}

		if (!RecordCameraPasses(commands, drawContext, *target)) {
			SDL_CancelGPUCommandBuffer(commands);
			SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
			return false;
		}

		// The readback has to see what the shaders produced, not the raw render
		RecordShaderChain(commands, camera, *target);

		SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commands);
		SDL_GPUTextureRegion region{
			.texture = target->ColorTexture,
			.w = width,
			.h = height,
			.d = 1,
		};
		SDL_GPUTextureTransferInfo destination{
			.transfer_buffer = transferBuffer,
			.offset = 0,
			.pixels_per_row = width,
			.rows_per_layer = height,
		};
		SDL_DownloadFromGPUTexture(copyPass, &region, &destination);
		SDL_EndGPUCopyPass(copyPass);

		SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
		if (!fence) {
			SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
			return false;
		}

		auto image = std::make_shared<EditableImage>();
		image->Name = EditableImage::DEFINITION.Name;

		PendingRenders.push_back({
			.Thread = thread,
			.ThreadReference = threadEngine->TakeThreadReference(thread),
			.Fence = fence,
			.TransferBuffer = transferBuffer,
			.Width = width,
			.Height = height,
			.Image = image,
		});

		return true;
	}

	void RenderProvider::PollRenders(ThreadEngine *threadEngine) {
		if (PendingRenders.empty() || !threadEngine) {
			return;
		}

		// Resuming a thread can start another render, so work from a snapshot
		// and keep whatever is still outstanding
		std::vector<PendingRender> stillPending;
		std::vector<PendingRender> ready;

		for (auto &pending : PendingRenders) {
			if (SDL_QueryGPUFence(Gpu, pending.Fence)) {
				ready.push_back(pending);
			} else {
				stillPending.push_back(pending);
			}
		}

		PendingRenders = std::move(stillPending);

		for (auto &pending : ready) {
			auto *mapped = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(Gpu, pending.TransferBuffer, false));
			if (mapped) {
				pending.Image->SetPixels((int)pending.Width, (int)pending.Height, mapped);
				SDL_UnmapGPUTransferBuffer(Gpu, pending.TransferBuffer);
			} else {
				SDL_Log("Failed to map a readback buffer: %s", SDL_GetError());
				pending.Image->SetPixels((int)pending.Width, (int)pending.Height, nullptr);
			}

			SDL_ReleaseGPUFence(Gpu, pending.Fence);
			SDL_ReleaseGPUTransferBuffer(Gpu, pending.TransferBuffer);

			StackValue<Instance::Pointer>::Push(pending.Thread, pending.Image);
			threadEngine->ResumeThread(pending.Thread, pending.ThreadReference, 1);
		}
	}

	void RenderProvider::Draw(DrawContext drawContext) {
		auto *camera = drawContext.Camera.get();

		// With shaders in play the world has to land somewhere the chain can
		// read, so it renders offscreen first and the result is blitted across.
		// Without them the swapchain is drawn straight into, exactly as before.
		bool useShaderChain = camera != nullptr && !camera->Shaders.empty();

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		if (useShaderChain) {
			CameraTarget *target = AcquireCameraTarget(camera, true);
			if (!target) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}

			if (!RecordCameraPasses(commands, drawContext, *target)) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}
			RecordShaderChain(commands, camera, *target);

			SDL_GPUTexture *swapchainTexture = nullptr;
			uint32_t swapchainWidth = 0, swapchainHeight = 0;
			if (!SDL_AcquireGPUSwapchainTexture(
					commands, Window, &swapchainTexture, &swapchainWidth, &swapchainHeight
				) ||
				!swapchainTexture) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}

			SDL_GPUBlitInfo blit{
				.source = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.destination = {.texture = swapchainTexture, .w = swapchainWidth, .h = swapchainHeight},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_LINEAR,
			};
			SDL_BlitGPUTexture(commands, &blit);
			SDL_SubmitGPUCommandBuffer(commands);
			return;
		}

		FrameContext frameContext;
		frameContext.Commands = commands;
		frameContext.WorldRoot = drawContext.WorldRoot;
		frameContext.Camera = drawContext.Camera;

		frameContext.ShadowMapTexture = ShadowMapTexture;
		frameContext.ShadowSampler = ShadowSampler;
		frameContext.LightDirection = glm::normalize(drawContext.LightDirection);

		if (DepthTexture) {
			frameContext.DepthTexture = DepthTexture;
		} else {
			SDL_CancelGPUCommandBuffer(frameContext.Commands);
			return;
		}

		auto swapchainResult = SDL_AcquireGPUSwapchainTexture(
			frameContext.Commands, Window, &frameContext.ColorTarget, &frameContext.Width, &frameContext.Height
		);
		if (!swapchainResult) {
			SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
			if (frameContext.Commands) {
				SDL_CancelGPUCommandBuffer(frameContext.Commands);
			};
			return;
		}

		if (!frameContext.Commands || !frameContext.ColorTarget || !frameContext.DepthTexture ||
			!frameContext.ShadowMapTexture) {
			SDL_CancelGPUCommandBuffer(frameContext.Commands);
			return;
		}

		SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));
		SDL_EndGPURenderPass(OpaquePass->Draw(Gpu, frameContext));

		SDL_SubmitGPUCommandBuffer(frameContext.Commands);
	}

	void RenderProvider::Resize(int width, int height) {
		if (width < 1 || height < 1) {
			return;
		}
		// SDL_SetGPUSwapchainParameters(Gpu, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
		SDL_SetGPUSwapchainParameters(Gpu, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

		if (DepthTexture != nullptr) {
			SDL_ReleaseGPUTexture(Gpu, DepthTexture);
		}

		SDL_GPUTextureCreateInfo depthInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
			.width = (uint32_t)width,
			.height = (uint32_t)height,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};

		DepthTexture = SDL_CreateGPUTexture(Gpu, &depthInfo);
	}
} // namespace gargantuan
