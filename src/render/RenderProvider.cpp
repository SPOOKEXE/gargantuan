// #define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/render/ShaderReflection.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

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
			if (target.HistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.HistoryTexture);
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

		if (WhiteTexture) {
			SDL_ReleaseGPUTexture(Gpu, WhiteTexture);
			WhiteTexture = nullptr;
		}

		if (FullscreenVertexShader) {
			SDL_ReleaseGPUShader(Gpu, FullscreenVertexShader);
			FullscreenVertexShader = nullptr;
		}

		if (OpaqueVertexShader) {
			SDL_ReleaseGPUShader(Gpu, OpaqueVertexShader);
			OpaqueVertexShader = nullptr;
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
		if (it->second.HistoryTexture) SDL_ReleaseGPUTexture(Gpu, it->second.HistoryTexture);
		if (it->second.DepthTexture) SDL_ReleaseGPUTexture(Gpu, it->second.DepthTexture);
		NeedsHistory.erase(it->first);
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
		if (target.HistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.HistoryTexture);
		target.ScratchTexture = nullptr;
		target.HistoryTexture = nullptr;
		if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);

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

	SDL_GPUTexture *RenderProvider::ResolveTextureSource(
		Camera *reader, const ShaderScript::TextureSource &source
	) {
		if (source.Image) {
			return AcquireImageTexture(source.Image.get());
		}

		// A camera's own target, sampled straight from the GPU
		if (source.Camera) {
			auto it = CameraTargets.find(source.Camera.get());
			if (it == CameraTargets.end()) {
				return nullptr;
			}

			// An edge that closes a cycle cannot see this frame's picture,
			// because the camera it reads has not been drawn yet. Give it the
			// finished previous frame rather than a half-written target.
			if (HistoryEdges.count({reader, source.Camera.get()}) && it->second.HistoryTexture) {
				return it->second.HistoryTexture;
			}

			return it->second.ColorTexture;
		}

		return nullptr;
	}

	void RenderProvider::RecordHistoryCopy(
		SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTarget &target
	) {
		if (!camera || !NeedsHistory.count(camera) || !target.ColorTexture) {
			return;
		}

		CameraTarget &mutableTarget = CameraTargets[camera];
		if (!mutableTarget.HistoryTexture) {
			SDL_GPUTextureCreateInfo info{
				.type = SDL_GPU_TEXTURETYPE_2D,
				.format = OFFSCREEN_FORMAT,
				.usage = CAMERA_TARGET_USAGE,
				.width = target.Width,
				.height = target.Height,
				.layer_count_or_depth = 1,
				.num_levels = 1,
			};
			mutableTarget.HistoryTexture = SDL_CreateGPUTexture(Gpu, &info);
			if (!mutableTarget.HistoryTexture) {
				return;
			}
		}

		// Taken after the chain has run, so the copy is the finished picture
		SDL_GPUBlitInfo blit{
			.source = {.texture = target.ColorTexture, .w = target.Width, .h = target.Height},
			.destination = {.texture = mutableTarget.HistoryTexture, .w = target.Width, .h = target.Height},
			.load_op = SDL_GPU_LOADOP_DONT_CARE,
			.filter = SDL_GPU_FILTER_NEAREST,
		};
		SDL_BlitGPUTexture(commands, &blit);
	}

	void RenderProvider::EnsureWhiteTexture() {
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
		}

		if (WhiteTexture) {
			return;
		}

		SDL_GPUTextureCreateInfo info{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = OFFSCREEN_FORMAT,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
			.width = 1,
			.height = 1,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		WhiteTexture = SDL_CreateGPUTexture(Gpu, &info);
		if (!WhiteTexture) {
			return;
		}

		SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = 4,
		};
		SDL_GPUTransferBuffer *transferBuffer = SDL_CreateGPUTransferBuffer(Gpu, &transferInfo);
		if (!transferBuffer) {
			return;
		}

		if (auto *mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(Gpu, transferBuffer, false))) {
			mapped[0] = mapped[1] = mapped[2] = mapped[3] = 255;
			SDL_UnmapGPUTransferBuffer(Gpu, transferBuffer);
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commands);
		SDL_GPUTextureTransferInfo source{
			.transfer_buffer = transferBuffer, .offset = 0, .pixels_per_row = 1, .rows_per_layer = 1
		};
		SDL_GPUTextureRegion destination{.texture = WhiteTexture, .w = 1, .h = 1, .d = 1};
		SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
		SDL_EndGPUCopyPass(copyPass);
		SDL_SubmitGPUCommandBuffer(commands);
		SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
	}

	void RenderProvider::ResolvePartTextures(const std::shared_ptr<WorldRoot> &worldRoot) {
		PartTextures.clear();
		if (!worldRoot) {
			return;
		}

		for (const auto &part : worldRoot->Parts) {
			if (!part || !part->SurfaceCamera) {
				continue;
			}

			auto it = CameraTargets.find(part->SurfaceCamera.get());
			if (it != CameraTargets.end() && it->second.ColorTexture) {
				PartTextures[part.get()] = it->second.ColorTexture;
			}
		}
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
	std::vector<uint8_t> RenderProvider::PackParameters(ShaderScript *shader, const CompiledShader &compiled) {
		const auto &layout = compiled.ParameterLayout;

		if (!layout.Found) {
			// No reflection, so fall back to one slot per parameter in the
			// order they were set
			const auto &slots = shader->GetPackedParameters();
			std::vector<uint8_t> packed(slots.size() * sizeof(glm::vec4));
			if (!slots.empty()) {
				std::memcpy(packed.data(), slots.data(), packed.size());
			}
			return packed;
		}

		std::vector<uint8_t> packed(layout.Size, 0);
		for (const auto &[name, value] : shader->GetParameters()) {
			const auto *member = layout.Find(name);
			// A parameter the shader never declared is simply ignored
			if (!member || member->Offset >= packed.size()) {
				continue;
			}

			// Clamp to the member's own size so setting a float cannot spill
			// into whatever was declared after it
			uint32_t writable = std::min<uint32_t>(member->Size, (uint32_t)sizeof(glm::vec4));
			writable = std::min<uint32_t>(writable, (uint32_t)(packed.size() - member->Offset));
			std::memcpy(packed.data() + member->Offset, &value, writable);
		}
		return packed;
	}

	std::string RenderProvider::GetShaderCacheKey(ShaderScript *shader, const char *stageExtension) {
		if (shader->HasBytecode()) {
			return "code:" + std::to_string((uintptr_t)shader) + ":" + std::to_string(shader->GetRevision());
		}
		return shader->Source + stageExtension;
	}

	RenderProvider::CompiledShader *RenderProvider::GetSurfaceShader(
		SurfaceShader *shader, SDL_GPUTextureFormat colorFormat
	) {
		std::string key = GetShaderCacheKey(shader, ".frag") + "#surface" + std::to_string((int)colorFormat);

		auto it = ShaderCache.find(key);
		if (it != ShaderCache.end()) {
			return it->second.Failed ? nullptr : &it->second;
		}

		CompiledShader &compiled = ShaderCache[key];
		compiled.Failed = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetShaderFormat(Gpu, format, extension, entrypoint);

		// A surface shader replaces only the fragment stage, so it reuses the
		// engine's own vertex stage and must match what that stage emits
		if (!OpaqueVertexShader) {
			size_t size = 0;
			void *code = LoadShaderBytes("opaque", ".vert", size);
			if (!code) {
				return nullptr;
			}

			SDL_GPUShaderCreateInfo info{
				.code_size = size,
				.code = static_cast<const Uint8 *>(code),
				.entrypoint = entrypoint.c_str(),
				.format = format,
				.stage = SDL_GPU_SHADERSTAGE_VERTEX,
				.num_samplers = 0,
				.num_storage_textures = 0,
				.num_storage_buffers = 0,
				.num_uniform_buffers = 2,
			};
			OpaqueVertexShader = SDL_CreateGPUShader(Gpu, &info);
			SDL_free(code);

			if (!OpaqueVertexShader) {
				SDL_Log("Failed to create the opaque vertex shader: %s", SDL_GetError());
				return nullptr;
			}
		}

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

		compiled.Resources = ShaderReflection::ReflectResources(code, size);
		SDL_GPUShaderCreateInfo fragmentInfo{
			.code_size = size,
			.code = static_cast<const Uint8 *>(code),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
			// the shadow map, then any images the script supplies
			.num_samplers = compiled.Resources.Found ? compiled.Resources.SampledImages : 1,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			.num_uniform_buffers = compiled.Resources.Found ? compiled.Resources.UniformBuffers : 2,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(code, size, 1);
		SDL_free(code);

		if (!fragment) {
			SDL_Log("Failed to create surface shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.GraphicsPipeline = PipelineBuilder()
										.SetVertexShader(OpaqueVertexShader)
										.SetFragmentShader(fragment)
										.SetColorEnabled(true)
										.SetColorFormat(colorFormat)
										.SetBlendingEnabled(true)
										.SetDepthEnabled(true)
										.SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
										.Build(Gpu);
		SDL_ReleaseGPUShader(Gpu, fragment);

		if (!compiled.GraphicsPipeline) {
			SDL_Log("Failed to build a surface pipeline for '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.Failed = false;
		return &compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetPostProcessShader(PostProcessShader *shader) {
		std::string key = GetShaderCacheKey(shader, ".frag");

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

		// Ask the shader what it needs rather than assuming
		compiled.Resources = ShaderReflection::ReflectResources(code, size);
		uint32_t samplerCount = compiled.Resources.Found ? compiled.Resources.SampledImages : 1;
		uint32_t uniformCount = compiled.Resources.Found ? compiled.Resources.UniformBuffers : 2;

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
			.num_uniform_buffers = uniformCount,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		// Parameters live at binding 1, with the engine builtins at binding 0
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(code, size, 1);
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

		compiled.Resources = ShaderReflection::ReflectResources(code, size);
		glm::vec3 threadGroupSize = shader->ThreadGroupSize;
		SDL_GPUComputePipelineCreateInfo info{
			.code_size = size,
			.code = static_cast<const Uint8 *>(code),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.num_samplers = 0,
			.num_readonly_storage_textures = compiled.Resources.Found ? compiled.Resources.ReadOnlyStorageImages : 1,
			.num_readonly_storage_buffers = 0,
			.num_readwrite_storage_textures = compiled.Resources.Found ? compiled.Resources.WriteStorageImages : 1,
			.num_readwrite_storage_buffers = 0,
			.num_uniform_buffers = compiled.Resources.Found ? compiled.Resources.UniformBuffers : 2,
			.threadcount_x = (uint32_t)glm::max(threadGroupSize.x, 1.0f),
			.threadcount_y = (uint32_t)glm::max(threadGroupSize.y, 1.0f),
			.threadcount_z = (uint32_t)glm::max(threadGroupSize.z, 1.0f),
		};
		compiled.ComputePipeline = SDL_CreateGPUComputePipeline(Gpu, &info);
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(code, size, 1);
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
		if (!camera || !target.ScratchTexture) {
			return;
		}

		// The built-in antialias pass runs last, after whatever the camera's
		// own chain did
		std::vector<std::shared_ptr<ShaderScript>> chain = camera->Shaders;
		if (camera->Antialiasing) {
			chain.push_back(GetAntialiasShader());
		}

		if (chain.empty()) {
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

			if (auto *post = shader->Cast<PostProcessShader>()) {
				CompiledShader *compiled = GetPostProcessShader(post);
				if (!compiled) {
					continue;
				}

				auto parameters = PackParameters(post, *compiled);
				// SDL rejects a zero-length uniform push, so always send a slot
				if (parameters.empty()) {
					parameters.resize(sizeof(glm::vec4), 0);
				}
				uint32_t parameterBytes = (uint32_t)parameters.size();

				SDL_GPUColorTargetInfo colorTarget{
					.texture = destination,
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.store_op = SDL_GPU_STOREOP_STORE,
				};
				// Slot 0 is always the camera's own output; the script's images
				// follow in the order they were set
				SDL_GPUTextureSamplerBinding bindings[1 + ShaderScript::MAXIMUM_IMAGES];
				bindings[0] = {.texture = source, .sampler = ShaderSampler};
				uint32_t bindingCount = 1;

				for (auto &source : post->GetTextureSources()) {
					SDL_GPUTexture *texture = ResolveTextureSource(camera, source);
					if (!texture || bindingCount > ShaderScript::MAXIMUM_IMAGES) {
						continue;
					}
					bindings[bindingCount++] = {.texture = texture, .sampler = ShaderSampler};
				}

				// The shader declared how many samplers it wants; anything else
				// would fail inside the driver with no explanation
				uint32_t declared = compiled->Resources.Found ? compiled->Resources.SampledImages : 1;
				if (bindingCount != declared) {
					SDL_Log(
						"Shader '%s' declares %u sampler(s) but %u were supplied; give it %u image(s) with SetImage",
						post->Source.empty() ? "<code>" : post->Source.c_str(),
						declared,
						bindingCount,
						declared > 0 ? declared - 1 : 0
					);
					continue;
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

				auto parameters = PackParameters(compute, *compiled);
				if (parameters.empty()) {
					parameters.resize(sizeof(glm::vec4), 0);
				}
				uint32_t parameterBytes = (uint32_t)parameters.size();

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

	// Shared by both draw paths: point the frame at a camera's surface shader,
	// its parameters, and any images it samples after the shadow map
	bool RenderProvider::PrepareSurfaceShader(
		FrameContext &frameContext,
		Camera *camera,
		SDL_GPUTextureFormat colorFormat,
		std::vector<uint8_t> &parameterStorage,
		std::vector<SDL_GPUTextureSamplerBinding> &samplerStorage
	) {
		if (!camera || !camera->SurfaceShader) {
			return false;
		}

		CompiledShader *surface = GetSurfaceShader(camera->SurfaceShader.get(), colorFormat);
		if (!surface) {
			return false;
		}

		parameterStorage = PackParameters(camera->SurfaceShader.get(), *surface);
		if (parameterStorage.empty()) {
			parameterStorage.resize(sizeof(glm::vec4), 0);
		}

		// Slot 0 is always the shadow map the engine's vertex stage set up
		samplerStorage.clear();
		samplerStorage.push_back({.texture = frameContext.ShadowMapTexture, .sampler = ShadowSampler});

		for (auto &source : camera->SurfaceShader->GetTextureSources()) {
			SDL_GPUTexture *texture = ResolveTextureSource(camera, source);
			if (!texture) {
				continue;
			}
			samplerStorage.push_back({.texture = texture, .sampler = ShaderSampler});
		}

		uint32_t declared = surface->Resources.Found ? surface->Resources.SampledImages : 1;
		if (samplerStorage.size() != declared) {
			SDL_Log(
				"Surface shader '%s' declares %u sampler(s) but %zu were supplied",
				camera->SurfaceShader->Source.empty() ? "<code>" : camera->SurfaceShader->Source.c_str(),
				declared,
				samplerStorage.size()
			);
			return false;
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
		}

		frameContext.SurfacePipeline = surface->GraphicsPipeline;
		frameContext.SurfaceParameters = parameterStorage.data();
		frameContext.SurfaceParameterBytes = (uint32_t)parameterStorage.size();
		frameContext.SurfaceSamplers = samplerStorage.data();
		frameContext.SurfaceSamplerCount = (uint32_t)samplerStorage.size();
		return true;
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
		EnsureWhiteTexture();
		ResolvePartTextures(drawContext.WorldRoot);
		frameContext.PartTextures = &PartTextures;
		frameContext.WhiteTexture = WhiteTexture;
		frameContext.SurfaceTextureSampler = ShaderSampler ? ShaderSampler : ShadowSampler;
		frameContext.Width = target.Width;
		frameContext.Height = target.Height;

		// A camera's SurfaceShader replaces the opaque pass's fragment stage
		std::vector<uint8_t> surfaceParameters;
		std::vector<SDL_GPUTextureSamplerBinding> surfaceSamplers;
		PrepareSurfaceShader(
			frameContext, drawContext.Camera.get(), OFFSCREEN_FORMAT, surfaceParameters, surfaceSamplers
		);

		// The shadow map only depends on the light, but it has to be recorded
		// into this command buffer for the opaque pass to sample it
		SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));
		SDL_EndGPURenderPass(OffscreenOpaquePass->Draw(Gpu, frameContext));
		return true;
	}

	std::shared_ptr<PostProcessShader> RenderProvider::GetAntialiasShader() {
		if (!AntialiasShader) {
			AntialiasShader = std::make_shared<PostProcessShader>();
			AntialiasShader->Name = "Antialias";
			AntialiasShader->Source = "antialias";
			// Below this much local contrast a pixel is passed through untouched
			AntialiasShader->SetNumber("Threshold", 0.0625f);
		}
		return AntialiasShader;
	}

	std::vector<Camera *> RenderProvider::GetSampledCameras(Camera *camera) {
		std::vector<Camera *> sampled;
		if (!camera) {
			return sampled;
		}

		auto collect = [&sampled](const std::shared_ptr<ShaderScript> &shader) {
			if (!shader) {
				return;
			}
			for (const auto &source : shader->GetTextureSources()) {
				if (source.Camera) {
					sampled.push_back(source.Camera.get());
				}
			}
		};

		for (const auto &shader : camera->Shaders) {
			collect(shader);
		}
		collect(camera->SurfaceShader);

		return sampled;
	}

	std::vector<Camera *> RenderProvider::GetRenderOrder(const std::vector<Camera *> &roots) {
		std::vector<Camera *> order;
		std::unordered_set<Camera *> finished;
		std::unordered_set<Camera *> visiting;

		// Post-order depth first, so a camera lands after everything it reads
		std::function<void(Camera *)> visit = [&](Camera *camera) {
			if (!camera || finished.count(camera)) {
				return;
			}

			if (visiting.count(camera)) {
				// Two cameras sampling each other cannot both be current. The
				// edge that closes the loop reads a previous-frame copy, which
				// is kept for exactly this reason.
				NeedsHistory.insert(camera);
				if (ReportedCycles.insert(camera).second) {
					SDL_Log(
						"Camera '%.*s' is part of a loop of cameras sampling each other; "
						"the edge that closes the loop reads its previous frame",
						(int)camera->Name.size(),
						camera->Name.data()
					);
				}
				return;
			}

			visiting.insert(camera);
			for (Camera *dependency : GetSampledCameras(camera)) {
				if (visiting.count(dependency)) {
					// Remember which reader takes the previous-frame path
					HistoryEdges.insert({camera, dependency});
				}
				visit(dependency);
			}
			visiting.erase(camera);

			finished.insert(camera);
			order.push_back(camera);
		};

		for (Camera *root : roots) {
			visit(root);
		}

		return order;
	}

	RenderProvider::WindowRegion RenderProvider::ComputeWindowRegion(
		const Camera &camera, int windowWidth, int windowHeight
	) {
		WindowRegion region;

		region.X = (int)glm::round(camera.WindowPosition.X.Scale * windowWidth) + camera.WindowPosition.X.Offset;
		region.Y = (int)glm::round(camera.WindowPosition.Y.Scale * windowHeight) + camera.WindowPosition.Y.Offset;
		region.Width = (int)glm::round(camera.WindowSize.X.Scale * windowWidth) + camera.WindowSize.X.Offset;
		region.Height = (int)glm::round(camera.WindowSize.Y.Scale * windowHeight) + camera.WindowSize.Y.Offset;

		// Trim anything hanging off the top or left, keeping the far edge put
		if (region.X < 0) {
			region.Width += region.X;
			region.X = 0;
		}
		if (region.Y < 0) {
			region.Height += region.Y;
			region.Y = 0;
		}

		region.Width = glm::clamp(region.Width, 0, glm::max(windowWidth - region.X, 0));
		region.Height = glm::clamp(region.Height, 0, glm::max(windowHeight - region.Y, 0));
		return region;
	}

	void RenderProvider::DrawComposite(const std::vector<DrawContext> &cameras) {
		if (cameras.empty()) {
			return;
		}

		// Panes can sample each other, so draw them in dependency order
		std::vector<Camera *> roots;
		roots.reserve(cameras.size());
		for (const auto &drawContext : cameras) {
			roots.push_back(drawContext.Camera.get());
		}

		std::unordered_map<Camera *, const DrawContext *> byCamera;
		for (const auto &drawContext : cameras) {
			byCamera[drawContext.Camera.get()] = &drawContext;
		}

		std::vector<DrawContext> ordered;
		for (Camera *camera : GetRenderOrder(roots)) {
			auto it = byCamera.find(camera);
			if (it != byCamera.end()) {
				ordered.push_back(*it->second);
			}
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		// Every camera is drawn offscreen first, because a swapchain texture
		// cannot be sampled by a shader chain or blitted from
		std::vector<std::pair<const CameraTarget *, const Camera *>> ready;
		for (const auto &drawContext : ordered) {
			auto *camera = drawContext.Camera.get();
			CameraTarget *target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
			if (!target) {
				continue;
			}

			DrawContext copy = drawContext;
			if (!RecordCameraPasses(commands, copy, *target)) {
				continue;
			}
			RecordShaderChain(commands, camera, *target);
			RecordHistoryCopy(commands, camera, *target);
			ready.emplace_back(target, camera);
		}

		SDL_GPUTexture *swapchainTexture = nullptr;
		uint32_t windowWidth = 0, windowHeight = 0;
		if (!SDL_AcquireGPUSwapchainTexture(commands, Window, &swapchainTexture, &windowWidth, &windowHeight) ||
			!swapchainTexture) {
			SDL_CancelGPUCommandBuffer(commands);
			return;
		}

		bool first = true;
		for (const auto &[target, camera] : ready) {
			WindowRegion region = ComputeWindowRegion(*camera, (int)windowWidth, (int)windowHeight);
			if (region.Width <= 0 || region.Height <= 0) {
				continue;
			}

			SDL_GPUBlitInfo blit{
				.source = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.destination =
					{
						.texture = swapchainTexture,
						.x = (uint32_t)region.X,
						.y = (uint32_t)region.Y,
						.w = (uint32_t)region.Width,
						.h = (uint32_t)region.Height,
					},
				// The first blit clears whatever the window held; the rest must
				// not wipe the panes drawn before them
				.load_op = first ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
				.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
				.filter = SDL_GPU_FILTER_LINEAR,
			};
			SDL_BlitGPUTexture(commands, &blit);
			first = false;
		}

		SDL_SubmitGPUCommandBuffer(commands);
	}

	void RenderProvider::DrawOffscreen(DrawContext drawContext) {
		auto *camera = drawContext.Camera.get();
		CameraTarget *target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
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
		RecordHistoryCopy(commands, camera, *target);
		SDL_SubmitGPUCommandBuffer(commands);
	}

	bool RenderProvider::RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine) {
		auto *camera = drawContext.Camera.get();

		// Anything this camera samples has to be drawn first, or it would read
		// whatever was in that target from a previous frame. Submissions run in
		// order, so drawing them now is enough.
		std::vector<Camera *> roots{camera};

		// A part showing another camera on its surface is a dependency too,
		// even though nothing in this camera's own shaders mentions it
		if (drawContext.WorldRoot) {
			for (const auto &part : drawContext.WorldRoot->Parts) {
				if (part && part->SurfaceCamera && part->SurfaceCamera.get() != camera) {
					roots.push_back(part->SurfaceCamera.get());
				}
			}
		}

		for (Camera *dependency : GetRenderOrder(roots)) {
			if (dependency == camera) {
				continue;
			}

			auto owned = dependency->weak_from_this().lock();
			if (!owned) {
				continue;
			}

			DrawOffscreen({
				.WorldRoot = drawContext.WorldRoot,
				.Camera = std::static_pointer_cast<Camera>(owned),
				.LightDirection = drawContext.LightDirection,
			});
		}
		CameraTarget *target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
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
		RecordHistoryCopy(commands, camera, *target);

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
		bool useShaderChain = camera != nullptr && (!camera->Shaders.empty() || camera->Antialiasing);

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
		EnsureWhiteTexture();
		ResolvePartTextures(drawContext.WorldRoot);
		frameContext.PartTextures = &PartTextures;
		frameContext.WhiteTexture = WhiteTexture;
		frameContext.SurfaceTextureSampler = ShaderSampler ? ShaderSampler : ShadowSampler;

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

		std::vector<uint8_t> surfaceParameters;
		std::vector<SDL_GPUTextureSamplerBinding> surfaceSamplers;
		PrepareSurfaceShader(frameContext, camera, SwapchainFormat, surfaceParameters, surfaceSamplers);

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
