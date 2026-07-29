
#include <algorithm>
#include <cmath>
#include "gargantuan/Profiler.hpp"
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

	void RenderProvider::RetireFrame(std::vector<SDL_GPUFence *> &fences) {
		if (!fences.empty()) {
			SDL_WaitForGPUFences(Gpu, true, fences.data(), (Uint32)fences.size());
			for (auto *fence : fences) {
				SDL_ReleaseGPUFence(Gpu, fence);
			}
		}
		fences.clear();
	}

	void RenderProvider::BeginFrame(int maximumFramesInFlight) {
		RedrawnThisFrame.clear();
		VelocityInUse = false;
		// Fixed through EndFrame so current-frame resources cannot be evicted.
		FrameIndex++;

		// Clamp to one to prevent an unbounded backlog.
		size_t maximum = (size_t)glm::max(maximumFramesInFlight, 1);
		while (FramesInFlight.size() >= maximum) {
			RetireFrame(FramesInFlight.front());
			FramesInFlight.pop_front();
		}
	}

	void RenderProvider::EndFrame() {
		// Stamp after all cameras so motion shares one previous frame.
		StampPreviousTransforms();

		if (!FrameFences.empty()) {
			FramesInFlight.push_back(std::move(FrameFences));
			FrameFences.clear();
		}
	}

	void RenderProvider::SubmitTracked(SDL_GPUCommandBuffer *commands) {
		// Fence failure loses pacing, not the submitted picture.
		if (SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands)) {
			FrameFences.push_back(fence);
		}
	}

	void RenderProvider::Destroy() {
		SDL_WaitForGPUIdle(Gpu);

		// GPU idle guarantees every fence has signalled.
		for (auto &fences : FramesInFlight) {
			for (auto *fence : fences) {
				SDL_ReleaseGPUFence(Gpu, fence);
			}
		}
		FramesInFlight.clear();
		for (auto *fence : FrameFences) {
			SDL_ReleaseGPUFence(Gpu, fence);
		}
		FrameFences.clear();

		// Pending readbacks cannot resume after teardown.
		for (auto &pending : PendingRenders) {
			if (pending.Fence) SDL_ReleaseGPUFence(Gpu, pending.Fence);
			if (pending.TransferBuffer) SDL_ReleaseGPUTransferBuffer(Gpu, pending.TransferBuffer);
		}
		PendingRenders.clear();

		for (auto &[_, target] : CameraTargets) {
			if (target.ColorTexture) SDL_ReleaseGPUTexture(Gpu, target.ColorTexture);
			if (target.ScratchTexture) SDL_ReleaseGPUTexture(Gpu, target.ScratchTexture);
			if (target.HistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.HistoryTexture);
			if (target.VelocityTexture) SDL_ReleaseGPUTexture(Gpu, target.VelocityTexture);
			if (target.ViewDepthTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthTexture);
			if (target.ViewDepthHistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthHistoryTexture);
			if (target.CacheTexture) SDL_ReleaseGPUTexture(Gpu, target.CacheTexture);
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
		CachedShaderRevisions.clear();

		if (WhiteTexture) {
			SDL_ReleaseGPUTexture(Gpu, WhiteTexture);
			WhiteTexture = nullptr;
		}

		if (FullscreenVertexShader) {
			SDL_ReleaseGPUShader(Gpu, FullscreenVertexShader);
			FullscreenVertexShader = nullptr;
		}

		if (WindowOverlayPipeline) {
			SDL_ReleaseGPUGraphicsPipeline(Gpu, WindowOverlayPipeline);
			WindowOverlayPipeline = nullptr;
		}
		WindowOverlays = {};

		if (OpaqueVertexShader) {
			SDL_ReleaseGPUShader(Gpu, OpaqueVertexShader);
			OpaqueVertexShader = nullptr;
		}

		if (ShaderSampler) {
			SDL_ReleaseGPUSampler(Gpu, ShaderSampler);
			ShaderSampler = nullptr;
		}

		if (PartSurfaceSampler) {
			SDL_ReleaseGPUSampler(Gpu, PartSurfaceSampler);
			PartSurfaceSampler = nullptr;
		}

		if (PointSampler) {
			SDL_ReleaseGPUSampler(Gpu, PointSampler);
			PointSampler = nullptr;
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
		if (VelocityPass) {
			VelocityPass->Destroy(Gpu);
		}

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
		if (it->second.VelocityTexture) SDL_ReleaseGPUTexture(Gpu, it->second.VelocityTexture);
		if (it->second.ViewDepthTexture) SDL_ReleaseGPUTexture(Gpu, it->second.ViewDepthTexture);
		if (it->second.ViewDepthHistoryTexture) SDL_ReleaseGPUTexture(Gpu, it->second.ViewDepthHistoryTexture);
		if (it->second.CacheTexture) SDL_ReleaseGPUTexture(Gpu, it->second.CacheTexture);
		if (it->second.DepthTexture) SDL_ReleaseGPUTexture(Gpu, it->second.DepthTexture);
		NeedsHistory.erase(it->first);
		VisibleSets.erase(it->first);
		CameraDrawCounts.erase(it->first);
		CameraTargets.erase(it);
	}

	// Camera targets serve every shader-chain input and output role.
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

		if (sized) {
			TargetGeneration++;
			target.ScratchTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
			if (!target.ScratchTexture) {
				SDL_Log("Failed to create a %ux%u shader scratch target: %s", width, height, SDL_GetError());
			}
			return &target;
		}

		if (target.ColorTexture) SDL_ReleaseGPUTexture(Gpu, target.ColorTexture);
		if (target.ScratchTexture) SDL_ReleaseGPUTexture(Gpu, target.ScratchTexture);
		if (target.HistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.HistoryTexture);
		if (target.VelocityTexture) SDL_ReleaseGPUTexture(Gpu, target.VelocityTexture);
		if (target.ViewDepthTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthTexture);
		if (target.ViewDepthHistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthHistoryTexture);
		if (target.CacheTexture) SDL_ReleaseGPUTexture(Gpu, target.CacheTexture);
		target.ScratchTexture = nullptr;
		target.HistoryTexture = nullptr;
		target.VelocityTexture = nullptr;
		target.ViewDepthTexture = nullptr;
		target.ViewDepthHistoryTexture = nullptr;
		target.CacheTexture = nullptr;
		if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);

		TargetGeneration++;
		target.ColorTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
		if (withScratch) {
			target.ScratchTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
		}

		// Must match the opaque pipeline's depth format.
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
		Camera *reader, const ShaderProperties::TextureSource &source
	) {
		if (source.Image) {
			return AcquireImageTexture(source.Image.get());
		}

		if (source.Camera) {
			auto it = CameraTargets.find(source.Camera.get());
			if (it == CameraTargets.end()) {
				return nullptr;
			}

			// Cycle-closing edges read the finished prior frame.
			if (HistoryEdges.count({reader, source.Camera.get()}) && it->second.HistoryTexture) {
				return it->second.HistoryTexture;
			}

			return it->second.ColorTexture;
		}

		// The shared antialias pass cannot name one owning camera.
		if (source.Render != Enums::RenderTexture::None) {
			auto it = CameraTargets.find(reader);
			if (it == CameraTargets.end()) {
				return nullptr;
			}

			switch (source.Render) {
			case Enums::RenderTexture::History:
				return it->second.HistoryTexture;
			case Enums::RenderTexture::Velocity:
				return it->second.VelocityTexture;
			case Enums::RenderTexture::Depth:
				return it->second.ViewDepthTexture;
			case Enums::RenderTexture::DepthHistory:
				return it->second.ViewDepthHistoryTexture;
			default:
				return nullptr;
			}
		}

		return nullptr;
	}

	void RenderProvider::SetWindowOverlay(size_t slot, std::shared_ptr<EditableImage> image, glm::vec2 position) {
		if (slot >= MAXIMUM_WINDOW_OVERLAYS) {
			return;
		}
		WindowOverlays[slot] = {std::move(image), position};
	}

	void RenderProvider::RecordWindowOverlay(
		SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height
	) {
		if (!commands || !target || width == 0 || height == 0 || WindowOverlayFailed) {
			return;
		}

		bool anything = false;
		for (const auto &entry : WindowOverlays) {
			anything = anything || entry.Image != nullptr;
		}
		if (!anything) {
			return;
		}

		if (!WindowOverlayPipeline) {
			// Report pipeline failure once.
			WindowOverlayFailed = true;

			SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
			std::string extension, entrypoint;
			GetShaderFormat(Gpu, format, extension, entrypoint);

			if (!FullscreenVertexShader) {
				size_t vertexSize = 0;
				void *vertexCode = LoadShaderBytes("fullscreen", ".vert", vertexSize);
				if (!vertexCode) {
					return;
				}

				SDL_GPUShaderCreateInfo vertexInfo{
					.code_size = vertexSize,
					.code = static_cast<const Uint8 *>(vertexCode),
					.entrypoint = entrypoint.c_str(),
					.format = format,
					.stage = SDL_GPU_SHADERSTAGE_VERTEX,
				};
				FullscreenVertexShader = SDL_CreateGPUShader(Gpu, &vertexInfo);
				SDL_free(vertexCode);

				if (!FullscreenVertexShader) {
					SDL_Log("Failed to create the fullscreen vertex shader: %s", SDL_GetError());
					return;
				}
			}

			size_t size = 0;
			void *code = LoadShaderBytes("window_overlay", ".frag", size);
			if (!code) {
				SDL_Log("Failed to load the window overlay shader");
				return;
			}

			SDL_GPUShaderCreateInfo fragmentInfo{
				.code_size = size,
				.code = static_cast<const Uint8 *>(code),
				.entrypoint = entrypoint.c_str(),
				.format = format,
				.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
				.num_samplers = 1,
				.num_storage_textures = 0,
				.num_storage_buffers = 0,
				.num_uniform_buffers = 1,
			};
			SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
			SDL_free(code);

			if (!fragment) {
				SDL_Log("Failed to create the window overlay shader: %s", SDL_GetError());
				return;
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
				return;
			}

			WindowOverlayFailed = false;
		}

		// Point-sample pixel text to avoid smearing narrow glyphs.
		EnsurePointSampler();
		if (!PointSampler) {
			return;
		}

		struct alignas(16) OverlayUniforms {
			glm::vec4 Target;
			glm::vec4 Rect;
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

			SDL_GPUTexture *texture = AcquireImageTexture(entry.Image.get());
			if (!texture) {
				continue;
			}

			OverlayUniforms uniforms{
				.Target = glm::vec4((float)width, (float)height, 0.0f, 0.0f),
				.Rect = glm::vec4(
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

	void RenderProvider::EnsurePointSampler() {
		if (PointSampler) {
			return;
		}

		SDL_GPUSamplerCreateInfo samplerInfo{
			.min_filter = SDL_GPU_FILTER_NEAREST,
			.mag_filter = SDL_GPU_FILTER_NEAREST,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
			.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		};
		PointSampler = SDL_CreateGPUSampler(Gpu, &samplerInfo);
	}

	SDL_GPUSampler *RenderProvider::GetSourceSampler(const ShaderProperties::TextureSource &source) {
		// Point-sample measurements; only pictures may interpolate.
		switch (source.Render) {
		case Enums::RenderTexture::Velocity:
		case Enums::RenderTexture::Depth:
		case Enums::RenderTexture::DepthHistory:
			EnsurePointSampler();
			if (PointSampler) {
				return PointSampler;
			}
			break;
		default:
			break;
		}
		return ShaderSampler;
	}

	RenderProvider::TemporalNeeds RenderProvider::GetTemporalNeeds(Camera *camera) {
		TemporalNeeds needs;
		if (!camera) {
			return needs;
		}

		auto consider = [&needs](const std::shared_ptr<ShaderScript> &shader) {
			if (!shader) {
				return;
			}

			if (shader->NeedsJitteredProjection()) {
				needs.Jitter = true;
			}

			for (const auto &source : shader->GetProperties()->GetTextureSources()) {
				switch (source.Render) {
				case Enums::RenderTexture::History:
					needs.History = true;
					break;
				// Velocity, depth, and depth history share one geometry pass.
				case Enums::RenderTexture::DepthHistory:
					needs.DepthHistory = true;
					needs.Motion = true;
					break;
				case Enums::RenderTexture::Velocity:
				case Enums::RenderTexture::Depth:
					needs.Motion = true;
					break;
				default:
					break;
				}
			}
		};

		// Include active antialiasing when deriving temporal needs.
		for (const auto &shader : BuildShaderChain(camera)) {
			consider(shader);
		}
		// Surface shaders may require velocity before opaque rendering.
		consider(camera->SurfaceShader);

		return needs;
	}

	void RenderProvider::EnsureTemporalTargets(
		SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, const TemporalNeeds &needs
	) {
		if (!camera || target.Width == 0 || target.Height == 0) {
			return;
		}

		if (needs.History) {
			// Allocate here; RecordHistoryCopy keeps it current.
			if (!target.HistoryTexture) {
				SDL_GPUTextureCreateInfo info{
					.type = SDL_GPU_TEXTURETYPE_2D,
					.format = OFFSCREEN_FORMAT,
					.usage = CAMERA_TARGET_USAGE,
					.width = target.Width,
					.height = target.Height,
					.layer_count_or_depth = 1,
					.num_levels = 1,
				};
				target.HistoryTexture = SDL_CreateGPUTexture(Gpu, &info);

				// Initialize before first read; black history is rejected.
				if (target.HistoryTexture && commands) {
					SDL_GPUColorTargetInfo clear{
						.texture = target.HistoryTexture,
						.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
						.load_op = SDL_GPU_LOADOP_CLEAR,
						.store_op = SDL_GPU_STOREOP_STORE,
					};
					SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &clear, 1, nullptr));
				}
			}
		}

		// Measurement textures need only geometry writes and shader reads.
		auto measurement = [&](SDL_GPUTexture *&texture, SDL_GPUTextureFormat format, const char *what) {
			if (texture) {
				return;
			}

			SDL_GPUTextureCreateInfo info{
				.type = SDL_GPU_TEXTURETYPE_2D,
				.format = format,
				.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
				.width = target.Width,
				.height = target.Height,
				.layer_count_or_depth = 1,
				.num_levels = 1,
			};
			texture = SDL_CreateGPUTexture(Gpu, &info);
			if (!texture) {
				SDL_Log("Failed to create a %ux%u %s target: %s", target.Width, target.Height, what, SDL_GetError());
			}
		};

		// Allocate both attachments because one pass declares and writes both.
		if (needs.Motion) {
			measurement(target.VelocityTexture, VELOCITY_FORMAT, "motion vector");
			measurement(target.ViewDepthTexture, VIEW_DEPTH_FORMAT, "view depth");
		}

		if (needs.DepthHistory && !target.ViewDepthHistoryTexture) {
			measurement(target.ViewDepthHistoryTexture, VIEW_DEPTH_FORMAT, "previous view depth");

			// Seed first depth history with the far plane.
			if (target.ViewDepthHistoryTexture && commands) {
				SDL_GPUColorTargetInfo clear{
					.texture = target.ViewDepthHistoryTexture,
					.clear_color = SDL_FColor{Camera::FAR_PLANE, 0.0f, 0.0f, 0.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR,
					.store_op = SDL_GPU_STOREOP_STORE,
				};
				SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &clear, 1, nullptr));
			}
		}
	}

	RenderPass *RenderProvider::GetVelocityPass() {
		if (!VelocityPass) {
			SDL_Log("Creating velocity pass");
			VelocityPass = CreateVelocityPass(Gpu);
		}
		return VelocityPass.get();
	}

	void RenderProvider::StampPreviousTransforms() {
		if (!Scene.WorldRoot) {
			return;
		}

		if (!VelocityInUse) {
			// Drop stale transforms when velocity demand stops.
			if (!TransformsStamped) {
				return;
			}

			for (const auto &part : Scene.WorldRoot->Parts) {
				if (part) {
					part->HasPreviousModelMatrix = false;
				}
			}
			TransformsStamped = false;
			return;
		}

		for (const auto &part : Scene.WorldRoot->Parts) {
			if (!part) {
				continue;
			}
			part->PreviousModelMatrix = part->GetModelMatrix();
			part->HasPreviousModelMatrix = true;
		}
		TransformsStamped = true;
	}

	void RenderProvider::RecordHistoryCopy(
		SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTarget &target
	) {
		if (!camera || !target.ColorTexture) {
			return;
		}

		TemporalNeeds needs = GetTemporalNeeds(camera);

		// Copy after the chain reads the previous depth history.
		if (needs.DepthHistory && target.ViewDepthTexture && target.ViewDepthHistoryTexture) {
			SDL_GPUBlitInfo blit{
				.source = {.texture = target.ViewDepthTexture, .w = target.Width, .h = target.Height},
				.destination = {.texture = target.ViewDepthHistoryTexture, .w = target.Width, .h = target.Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &blit);
		}

		// Cycles persist; ordinary history bindings are recomputed each frame.
		if (!NeedsHistory.count(camera) && !needs.History) {
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

		// Copy only the finished chain output.
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

		if (!PartSurfaceSampler) {
			SDL_GPUSamplerCreateInfo samplerInfo{
				.min_filter = SDL_GPU_FILTER_LINEAR,
				.mag_filter = SDL_GPU_FILTER_LINEAR,
				.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
				.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
				.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
				.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
			};
			PartSurfaceSampler = SDL_CreateGPUSampler(Gpu, &samplerInfo);
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
		G_PROFILE("Part Textures");

		// Reuse until its inputs change; it is camera-independent.
		if (PartTexturesResolved && ResolvedSurfaceSignature == SurfaceSignature) {
			return;
		}

		PartTextures.clear();
		ResolvedSurfaceSignature = SurfaceSignature;
		PartTexturesResolved = true;

		if (!worldRoot || !WorldHasSurfaces) {
			return;
		}

		for (const auto &part : worldRoot->Parts) {
			if (!part) {
				continue;
			}

		if (part->SurfaceCamera) {
				auto it = CameraTargets.find(part->SurfaceCamera.get());
				if (it != CameraTargets.end() && it->second.ColorTexture) {
					PartTextures[part.get()] = it->second.ColorTexture;
					continue;
				}
			}

			if (part->SurfaceImage) {
				if (SDL_GPUTexture *texture = AcquireImageTexture(part->SurfaceImage.get())) {
					PartTextures[part.get()] = texture;
				}
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

	std::vector<uint8_t> RenderProvider::PackParameters(ShaderScript *shader, const CompiledShader &compiled) {
		const auto &layout = compiled.ParameterLayout;

		if (!layout.Found) {
			// Without reflection, pack one slot per parameter in set order.
			const auto &slots = shader->GetProperties()->GetPackedParameters();
			std::vector<uint8_t> packed(slots.size() * sizeof(glm::vec4));
			if (!slots.empty()) {
				std::memcpy(packed.data(), slots.data(), packed.size());
			}
			return packed;
		}

		std::vector<uint8_t> packed(layout.Size, 0);
		for (const auto &[name, value] : shader->GetProperties()->GetParameters()) {
			const auto *member = layout.Find(name);
			if (!member || member->Offset >= packed.size()) {
				continue;
			}

			// Clamp writes to the reflected member size.
			uint32_t writable = std::min<uint32_t>(member->Size, (uint32_t)sizeof(glm::vec4));
			writable = std::min<uint32_t>(writable, (uint32_t)(packed.size() - member->Offset));
			std::memcpy(packed.data() + member->Offset, &value, writable);
		}
		return packed;
	}

	std::string RenderProvider::GetShaderCacheKey(ShaderScript *shader, const char *stageExtension) {
		if (shader->HasBytecode()) {
			return "code:" + std::to_string(shader->GetSerial()) + ":" + std::to_string(shader->GetRevision());
		}
		return shader->Source + stageExtension;
	}

	RenderProvider::CompiledShader *RenderProvider::FindCachedShader(const std::string &key) {
		auto it = ShaderCache.find(key);
		if (it == ShaderCache.end()) {
			return nullptr;
		}

		it->second.LastUsedFrame = FrameIndex;
		return &it->second;
	}

	void RenderProvider::ReleaseCachedShader(const std::string &key) {
		auto it = ShaderCache.find(key);
		if (it == ShaderCache.end()) {
			return;
		}

		if (it->second.GraphicsPipeline) SDL_ReleaseGPUGraphicsPipeline(Gpu, it->second.GraphicsPipeline);
		if (it->second.ComputePipeline) SDL_ReleaseGPUComputePipeline(Gpu, it->second.ComputePipeline);
		ShaderCache.erase(it);
	}

	void RenderProvider::DropSupersededShader(ShaderScript *shader) {
		if (!shader || !shader->HasBytecode()) {
			return;
		}

		uint64_t serial = shader->GetSerial();
		uint64_t revision = shader->GetRevision();

		auto it = CachedShaderRevisions.find(serial);
		if (it == CachedShaderRevisions.end()) {
			CachedShaderRevisions[serial] = revision;
			return;
		}

		if (it->second == revision) {
			return;
		}

		// Revisions are monotonic; remove all entries for the old revision.
		std::string stem = "code:" + std::to_string(serial) + ":" + std::to_string(it->second);
		for (auto entry = ShaderCache.begin(); entry != ShaderCache.end();) {
			if (entry->first.rfind(stem, 0) != 0) {
				++entry;
				continue;
			}

			// Keep current-frame pipelines alive until unsubmitted buffers finish.
			if (entry->second.LastUsedFrame == FrameIndex) {
				++entry;
				continue;
			}

			if (entry->second.GraphicsPipeline) SDL_ReleaseGPUGraphicsPipeline(Gpu, entry->second.GraphicsPipeline);
			if (entry->second.ComputePipeline) SDL_ReleaseGPUComputePipeline(Gpu, entry->second.ComputePipeline);
			entry = ShaderCache.erase(entry);
		}

		it->second = revision;
	}

	void RenderProvider::TrimShaderCache() {
		while (ShaderCache.size() > MAXIMUM_CACHED_SHADERS) {
			const std::string *oldestKey = nullptr;
			uint64_t oldestFrame = 0;

			for (const auto &[key, compiled] : ShaderCache) {
				// Never evict pipelines handed to this frame.
				if (compiled.LastUsedFrame == FrameIndex) {
					continue;
				}

				if (!oldestKey || compiled.LastUsedFrame < oldestFrame) {
					oldestKey = &key;
					oldestFrame = compiled.LastUsedFrame;
				}
			}

			// Exceed the bound temporarily when every entry is in use this frame.
			if (!oldestKey) {
				return;
			}

			ReleaseCachedShader(*oldestKey);
		}
	}

	RenderProvider::CompiledShader &RenderProvider::InsertCachedShader(
		const std::string &key, ShaderScript *shader
	) {
		DropSupersededShader(shader);

		CompiledShader &compiled = ShaderCache[key];
		compiled.LastUsedFrame = FrameIndex;

		// Insert then trim to the true ceiling; the returned current entry is safe.
		TrimShaderCache();
		return compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetSurfaceShader(
		SurfaceShader *shader, SDL_GPUTextureFormat colorFormat
	) {
		std::string key = GetShaderCacheKey(shader, ".frag") + "#surface" + std::to_string((int)colorFormat);

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->Failed ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
		compiled.Failed = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetShaderFormat(Gpu, format, extension, entrypoint);

		// Surface shaders replace only the fragment stage.
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
			// Shadow map followed by script images.
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

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->Failed ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
		compiled.Failed = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetShaderFormat(Gpu, format, extension, entrypoint);

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

		// Prefer runtime bytecode; fall back to the built asset.
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
			.num_uniform_buffers = uniformCount,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		// Parameters use binding 1; engine builtins use binding 0.
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(code, size, 1);
		SDL_free(code);

		if (!fragment) {
			SDL_Log("Failed to create fragment shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		// gl_VertexIndex supplies the fullscreen triangle; disable vertex input and culling.
		compiled.GraphicsPipeline = PipelineBuilder()
										.SetVertexShader(FullscreenVertexShader)
										.SetFragmentShader(fragment)
										.SetVertexInputEnabled(false)
										.SetCullingEnabled(false)
										.SetColorEnabled(true)
										.SetColorFormat(OFFSCREEN_FORMAT)
										.SetDepthEnabled(false)
										.Build(Gpu);

		// Pipeline retains the shader module.
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

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->Failed ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
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

	std::vector<std::shared_ptr<ShaderScript>> RenderProvider::BuildShaderChain(Camera *camera) {
		if (!camera) {
			return {};
		}

		// Built-in antialiasing runs after the camera chain.
		std::vector<std::shared_ptr<ShaderScript>> chain = camera->Shaders;
		if (camera->Antialiasing) {
			chain.push_back(GetAntialiasShader());
		}
		return chain;
	}

	size_t RenderProvider::FindCacheCut(const std::vector<std::shared_ptr<ShaderScript>> &chain) {
		for (size_t index = 0; index < chain.size(); index++) {
			if (chain[index] && chain[index]->NeedsRedrawEveryFrame()) {
				return index;
			}
		}
		// No always-redraw pass: cache the whole chain.
		return chain.size();
	}

	void RenderProvider::RecordShaderChain(
		SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, size_t firstShader, bool writeCache
	) {
		G_PROFILE("Shader Chain");
		if (!camera || !target.ScratchTexture) {
			return;
		}

		std::vector<std::shared_ptr<ShaderScript>> chain = BuildShaderChain(camera);
		if (chain.empty() || firstShader >= chain.size()) {
			return;
		}

		size_t cut = FindCacheCut(chain);

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
			.Jitter = glm::vec4(camera->Jitter, camera->PreviousJitter),
		};

		// source always names the latest ping-pong output.
		SDL_GPUTexture *source = target.ColorTexture;
		SDL_GPUTexture *destination = target.ScratchTexture;

		for (size_t index = firstShader; index < chain.size(); index++) {
			// Snapshot immediately before the always-redraw tail.
			if (index == cut && writeCache && target.CacheTexture) {
				SDL_GPUBlitInfo snapshot{
					.source = {.texture = source, .w = target.Width, .h = target.Height},
					.destination = {.texture = target.CacheTexture, .w = target.Width, .h = target.Height},
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.filter = SDL_GPU_FILTER_NEAREST,
				};
				SDL_BlitGPUTexture(commands, &snapshot);
			}

			auto &shader = chain[index];
			// Skip scripts with no code source.
			if (!shader || (shader->Source.empty() && !shader->HasBytecode())) {
				continue;
			}

			if (auto *post = shader->Cast<PostProcessShader>()) {
				CompiledShader *compiled = GetPostProcessShader(post);
				if (!compiled) {
					continue;
				}

				auto parameters = PackParameters(post, *compiled);
				// SDL rejects zero-length uniform pushes.
				if (parameters.empty()) {
					parameters.resize(sizeof(glm::vec4), 0);
				}
				uint32_t parameterBytes = (uint32_t)parameters.size();

				SDL_GPUColorTargetInfo colorTarget{
					.texture = destination,
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.store_op = SDL_GPU_STOREOP_STORE,
				};
				// Slot 0 is camera output; script images follow set order.
				SDL_GPUTextureSamplerBinding bindings[1 + ShaderProperties::MAXIMUM_IMAGES];
				bindings[0] = {.texture = source, .sampler = ShaderSampler};
				uint32_t bindingCount = 1;

				for (auto &bound : post->GetProperties()->GetTextureSources()) {
					SDL_GPUTexture *texture = ResolveTextureSource(camera, bound);
					if (!texture || bindingCount > ShaderProperties::MAXIMUM_IMAGES) {
						continue;
					}
					bindings[bindingCount++] = {.texture = texture, .sampler = GetSourceSampler(bound)};
				}

				// Validate sampler count before the driver does.
				uint32_t declared = compiled->Resources.Found ? compiled->Resources.SampledImages : 1;
				if (bindingCount != declared) {
					SDL_Log(
						"Shader '%s' declares %u sampler(s) but %u were supplied; give it %u image(s) with "
					"Properties:SetImage",
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

				// Round up edge groups; the shader discards out-of-bounds lanes.
				uint32_t groupX = (uint32_t)glm::max(compute->ThreadGroupSize.x, 1.0f);
				uint32_t groupY = (uint32_t)glm::max(compute->ThreadGroupSize.y, 1.0f);
				SDL_DispatchGPUCompute(
					pass, (target.Width + groupX - 1) / groupX, (target.Height + groupY - 1) / groupY, 1
				);
				SDL_EndGPUComputePass(pass);
			} else {
				// Bare ShaderScript has no executable stage.
				continue;
			}

			std::swap(source, destination);
		}

		// Normalize final output into the camera texture.
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

	// Prepare surface pipeline, parameters, and post-shadow-map images.
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

		// Create sampler before constructing bindings.
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

		// Slot 0 is always the shadow map.
		samplerStorage.clear();
		samplerStorage.push_back({.texture = frameContext.ShadowMapTexture, .sampler = ShadowSampler});

		for (auto &source : camera->SurfaceShader->GetProperties()->GetTextureSources()) {
			SDL_GPUTexture *texture = ResolveTextureSource(camera, source);
			if (!texture) {
				continue;
			}
			samplerStorage.push_back({.texture = texture, .sampler = GetSourceSampler(source)});
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
		G_PROFILE("Camera Passes");
		if (!commands || !target.ColorTexture || !target.DepthTexture || !ShadowMapTexture) {
			return false;
		}

		Camera *camera = drawContext.Camera.get();
		TemporalNeeds needs = GetTemporalNeeds(camera);
		if (camera) {
			EnsureTemporalTargets(commands, camera, CameraTargets[camera], needs);
			// Advance jitter only with the world pixels it samples.
			camera->AdvanceJitter(needs.Jitter);
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
		// Velocity pass requires both declared attachments.
		bool motion = needs.Motion && target.VelocityTexture && target.ViewDepthTexture;
		frameContext.VelocityTarget = motion ? target.VelocityTexture : nullptr;
		frameContext.ViewDepthTarget = motion ? target.ViewDepthTexture : nullptr;
		EnsureWhiteTexture();
		ResolvePartTextures(drawContext.WorldRoot);
		frameContext.PartTextures = &PartTextures;
		frameContext.WhiteTexture = WhiteTexture;
		frameContext.SurfaceTextureSampler = PartSurfaceSampler ? PartSurfaceSampler : ShadowSampler;
		frameContext.Width = target.Width;
		frameContext.Height = target.Height;
		// Guarantee visibility here for paths that bypass PlanRedraw.
		frameContext.Visible = &EnsureVisibleSet(
			drawContext.Camera.get(),
			drawContext.WorldRoot,
			drawContext.LightDirection,
			ComputeCameraSignature(drawContext.Camera.get())
		);

		// SurfaceShader replaces the opaque fragment stage.
		std::vector<uint8_t> surfaceParameters;
		std::vector<SDL_GPUTextureSamplerBinding> surfaceSamplers;
		PrepareSurfaceShader(
			frameContext, drawContext.Camera.get(), OFFSCREEN_FORMAT, surfaceParameters, surfaceSamplers
		);

		// Record shadow production before opaque sampling in this buffer.
		{
			G_PROFILE("Shadow");
			SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));
		}

		// Record velocity first so SurfaceShader reads this frame's values.
		if (frameContext.VelocityTarget) {
			if (RenderPass *velocity = GetVelocityPass()) {
				G_PROFILE("Motion");
				SDL_EndGPURenderPass(velocity->Draw(Gpu, frameContext));
				VelocityInUse = true;
			}
		}

		{
			G_PROFILE("Opaque");
			SDL_EndGPURenderPass(OffscreenOpaquePass->Draw(Gpu, frameContext));
		}

		// Store unjittered motion; jitter changes sampling, not position.
		if (camera && needs.Motion) {
			camera->PreviousViewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();
			camera->HasPreviousViewProjection = true;
		}

		return true;
	}

	void RenderProvider::SetAntialiasOverride(std::shared_ptr<ShaderScript> shader) {
		AntialiasOverride = std::move(shader);
	}

	std::shared_ptr<ShaderScript> RenderProvider::GetAntialiasShader() {
		// Override replaces but does not destroy the reusable built-in pass.
		if (AntialiasOverride) {
			return AntialiasOverride;
		}

		if (!AntialiasShader) {
			AntialiasShader = std::make_shared<PostProcessShader>();
			AntialiasShader->Name = "Antialias";
			AntialiasShader->Source = "antialias";
			// Preserve pixels below this local-contrast threshold.
			AntialiasShader->GetProperties()->SetNumber("Threshold", 0.0625f);
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
			for (const auto &source : shader->GetProperties()->GetTextureSources()) {
				if (source.Camera) {
					sampled.push_back(source.Camera.get());
				}
			}
		};

		for (const auto &shader : BuildShaderChain(camera)) {
			collect(shader);
		}
		collect(camera->SurfaceShader);

		return sampled;
	}

	std::vector<Camera *> RenderProvider::GetRenderOrder(const std::vector<Camera *> &roots) {
		std::vector<Camera *> order;
		std::unordered_set<Camera *> finished;
		std::unordered_set<Camera *> visiting;

		// Post-order DFS draws every input before its reader.
		std::function<void(Camera *)> visit = [&](Camera *camera) {
			if (!camera || finished.count(camera)) {
				return;
			}

			if (visiting.count(camera)) {
				// Break sampling cycles with a prior-frame read.
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
					// Record the reader that must use prior-frame data.
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

		// Clip top/left while preserving the far edge.
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

		// Draw panes in sampling-dependency order.
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

		// Draw offscreen because swapchain textures cannot be sampled or blitted from.
		std::vector<std::pair<const CameraTarget *, const Camera *>> ready;
		for (const auto &drawContext : ordered) {
			auto *camera = drawContext.Camera.get();

			// Reuse a still pane's prior target.
			DrawContext copy = drawContext;
			bool recorded = false;
			CameraTarget *target = RecordCamera(commands, copy, recorded);
			if (!target) {
				continue;
			}
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
				// Only the first pane clears the window.
				.load_op = first ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
				.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
				.filter = SDL_GPU_FILTER_LINEAR,
			};
			SDL_BlitGPUTexture(commands, &blit);
			first = false;
		}

		RecordWindowOverlay(commands, swapchainTexture, windowWidth, windowHeight);
		SubmitTracked(commands);
	}

	namespace {
		// 64-bit FNV-1a; a collision can skip one redraw.
		inline void MixBits(uint64_t &hash, uint64_t value) {
			hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
		}

		inline void MixFloat(uint64_t &hash, float value) {
			// Hash canonical bits so signed zero and NaN remain stable.
			uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			MixBits(hash, bits);
		}

		inline void MixVec3(uint64_t &hash, const glm::vec3 &value) {
			MixFloat(hash, value.x);
			MixFloat(hash, value.y);
			MixFloat(hash, value.z);
		}

		inline void MixPointer(uint64_t &hash, const void *pointer) {
			MixBits(hash, (uint64_t)(uintptr_t)pointer);
		}

		// World-space side planes; positive distance is inside.
		struct SidePlanes {
			glm::vec4 Planes[4];
		};

		// Omit ineffective near/far planes and stay depth-convention independent.
		// Side planes still reject geometry behind the eye.
		SidePlanes ExtractSidePlanes(const glm::mat4 &viewProjection) {
			// GLM rows are the nth component of each column.
			auto row = [&](int index) {
				return glm::vec4(
					viewProjection[0][index],
					viewProjection[1][index],
					viewProjection[2][index],
					viewProjection[3][index]
				);
			};

			glm::vec4 x = row(0), y = row(1), w = row(3);
			SidePlanes planes{{w + x, w - x, w + y, w - y}};

			// Normalize for world-unit radius comparisons.
			for (auto &plane : planes.Planes) {
				float length = glm::length(glm::vec3(plane));
				if (length > 0.0f) {
					plane /= length;
				}
			}
			return planes;
		}

		// Sphere test is specialized because every part uses it; casters use capsules.
		bool SphereInside(const SidePlanes &planes, glm::vec3 centre, float radius) {
			// Plain floats avoid four per-part temporary/call pairs.
			for (const auto &plane : planes.Planes) {
				float distance = plane.x * centre.x + plane.y * centre.y + plane.z * centre.z + plane.w;
				if (distance < -radius) {
					return false;
				}
			}
			return true;
		}

		bool CapsuleInside(const SidePlanes &planes, glm::vec3 from, glm::vec3 to, float radius) {
			for (const auto &plane : planes.Planes) {
				// Reject only when both capsule ends lie outside one plane.
				float near = plane.x * from.x + plane.y * from.y + plane.z * from.z + plane.w;
				float far = plane.x * to.x + plane.y * to.y + plane.z * to.z + plane.w;
				if (near < -radius && far < -radius) {
					return false;
				}
			}
			return true;
		}

		// Matches ShadowPass depth; include offscreen casters to prevent frozen shadows.
		constexpr float SHADOW_CAST_REACH = 200.0f;
	} // namespace

	uint64_t RenderProvider::ComputeSceneSignature(
		const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection
	) const {
		uint64_t hash = 0xCBF29CE484222325ull;
		MixVec3(hash, lightDirection);

		if (!world) {
			return hash;
		}

		// Count distinguishes simultaneous removal and insertion.
		MixBits(hash, world->Parts.size());

		// QuickHash invalidates the prior surface-presence answer.
		const bool hadSurfaces = WorldHasSurfaces;

		// Derive while these fields are already hot.
		WorldHasSurfaceCameras = false;
		WorldHasSurfaces = false;

		// Hash while these fields are already read.
		uint64_t surfaces = 0x9E3779B97F4A7C15ull;
		MixBits(surfaces, TargetGeneration);
		MixBits(surfaces, world->Parts.size());

		for (const auto &part : world->Parts) {
			if (!part) {
				MixBits(hash, 0);
				continue;
			}

			// Pointer catches replacement; QuickHash catches property writes.
			MixPointer(hash, part.get());
			MixBits(hash, part->QuickHash);

			// Read once: every shared_ptr deref is a call here.
			Camera *surfaceCamera = part->SurfaceCamera.get();
			EditableImage *surfaceImage = part->SurfaceImage.get();

			// Detect here so later walks skip absent surface sources.
			if (surfaceCamera) {
				WorldHasSurfaceCameras = true;
				WorldHasSurfaces = true;
			} else if (surfaceImage) {
				WorldHasSurfaces = true;
			}

			// Pointer identifies the source; revision/count identifies its
			// content. Only the parts carrying one: a part gaining a surface
			// bumps its QuickHash, which is mixed above.
			if (hadSurfaces && (surfaceCamera || surfaceImage)) {
				MixPointer(hash, surfaceCamera);
				MixBits(hash, GetCameraDrawCount(surfaceCamera));
				MixPointer(hash, surfaceImage);
				MixBits(hash, surfaceImage ? surfaceImage->GetRevision() : 0);
			}
		}

		SurfaceSignature = surfaces;
		return hash;
	}

	uint64_t RenderProvider::GetCameraDrawCount(Camera *camera) const {
		if (!camera) {
			return 0;
		}

		auto it = CameraDrawCounts.find(camera);
		return it == CameraDrawCounts.end() ? 0 : it->second;
	}

	void RenderProvider::CountCameraDraw(Camera *camera) {
		if (camera) {
			CameraDrawCounts[camera]++;
		}
	}

	void RenderProvider::ComputeVisibleSet(
		Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, VisibleSet &out
	) {
		G_PROFILE("Frustum Walk");
		out.InView.clear();
		out.ShadowsIntoView.clear();
		out.InViewList.clear();
		out.ShadowList.clear();

		// Only surface-camera redraw checks require lookup sets.
		const bool needSets = WorldHasSurfaceCameras;
		const bool worldHasSurfaces = WorldHasSurfaces;

		if (world) {
			// Reserve once for large worlds.
			out.InViewList.reserve(world->Parts.size());
			out.ShadowList.reserve(world->Parts.size());
		}

		uint64_t hash = 0xCBF29CE484222325ull;
		MixVec3(hash, lightDirection);

		if (!camera || !world) {
			out.Signature = hash;
			return;
		}

		SidePlanes planes = ExtractSidePlanes(camera->GetProjectionMatrix() * camera->GetViewMatrix());
		// Shadows extend opposite the toward-light vector.
		glm::vec3 shadowStep = -glm::normalize(lightDirection) * SHADOW_CAST_REACH;

		uint64_t visible = 0;

		// Chunking keeps probes cheaper than work and improves field locality.
		constexpr size_t CHUNK = 256;
		Profiler *profiler = Profiler::GetCurrent();
		const bool measuring = profiler && profiler->IsEnabled();
		uint64_t cullNanoseconds = 0;
		uint64_t gatherNanoseconds = 0;
		uint64_t signatureNanoseconds = 0;

		// Reused across chunks and frames.
		CullScratch.resize(CHUNK);

		const size_t total = world->Parts.size();
		for (size_t start = 0; start < total; start += CHUNK) {
			const size_t stop = std::min(start + CHUNK, total);

			uint64_t cullStart = measuring ? SDL_GetTicksNS() : 0;
			for (size_t index = start; index < stop; index++) {
				const auto &part = world->Parts[index];
				CullResult &result = CullScratch[index - start];
				if (!part) {
					result.InView = false;
					result.ShadowReaches = false;
					continue;
				}

				// Half-diagonal bounds every box rotation conservatively.
				const glm::vec3 &size = part->Size;
				float radius = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z) * 0.5f;
				const glm::vec3 &centre = part->CFrame.Position;

				result.InView = SphereInside(planes, centre, radius);
				// Sweep only offscreen casters along their shadow reach.
				result.ShadowReaches = !result.InView && part->CastShadow &&
					CapsuleInside(planes, centre, centre + shadowStep, radius);
			}
			if (measuring) {
				cullNanoseconds += SDL_GetTicksNS() - cullStart;
			}

			uint64_t gatherStart = measuring ? SDL_GetTicksNS() : 0;
			for (size_t index = start; index < stop; index++) {
				const auto &part = world->Parts[index];
				if (!part) {
					continue;
				}

				const CullResult &result = CullScratch[index - start];
				if (result.InView) {
					if (needSets) {
						out.InView.insert(part.get());
					}
					out.InViewList.push_back(part.get());
				}
				// ShadowList is a superset of visible shadow casters.
				if (part->CastShadow && (result.InView || result.ShadowReaches)) {
					if (needSets) {
						out.ShadowsIntoView.insert(part.get());
					}
					out.ShadowList.push_back(part.get());
				}
			}
			if (measuring) {
				gatherNanoseconds += SDL_GetTicksNS() - gatherStart;
			}

			uint64_t signatureStart = measuring ? SDL_GetTicksNS() : 0;
			for (size_t index = start; index < stop; index++) {
				const auto &part = world->Parts[index];
				if (!part) {
					continue;
				}

				const CullResult &result = CullScratch[index - start];
				// Hash visible parts and offscreen casters affecting view.
				if (!result.InView && !result.ShadowReaches) {
					continue;
				}

				visible++;
				MixPointer(hash, part.get());
				MixBits(hash, part->QuickHash);
				// QuickHash detects newly added surfaces omitted here.
				if (worldHasSurfaces && (part->SurfaceCamera || part->SurfaceImage)) {
					MixPointer(hash, part->SurfaceCamera.get());
					MixBits(hash, GetCameraDrawCount(part->SurfaceCamera.get()));
					MixPointer(hash, part->SurfaceImage.get());
					MixBits(hash, part->SurfaceImage ? part->SurfaceImage->GetRevision() : 0);
				}
			}
			if (measuring) {
				signatureNanoseconds += SDL_GetTicksNS() - signatureStart;
			}
		}

		if (measuring) {
			profiler->AddZoneTime("Cull", cullNanoseconds, total);
			profiler->AddZoneTime("Gather", gatherNanoseconds, total);
			profiler->AddZoneTime("Signature", signatureNanoseconds, total);
		}

		// Include visible count, especially for empty views.
		MixBits(hash, visible);
		out.Signature = hash;
	}

	const VisibleSet &RenderProvider::EnsureVisibleSet(
		Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t cameraSignature
	) {
		VisibleSet &set = VisibleSets[camera];

		// Reuse one walk while scene and camera stamps match.
		if (set.Walked && set.SceneStamp == SceneSignature && set.CameraStamp == cameraSignature) {
			return set;
		}

		ComputeVisibleSet(camera, world, lightDirection, set);
		set.SceneStamp = SceneSignature;
		set.CameraStamp = cameraSignature;
		set.Walked = true;
		return set;
	}

	uint64_t RenderProvider::ComputeCameraSignature(Camera *camera) {
		uint64_t hash = 0x100000001B3ull;
		if (!camera) {
			return hash;
		}

		MixVec3(hash, camera->CFrame.Position);
		MixVec3(hash, camera->CFrame.GetRightVector());
		MixVec3(hash, camera->CFrame.GetUpVector());
		MixVec3(hash, camera->CFrame.GetLookVector());

		MixFloat(hash, camera->FieldOfView);
		MixFloat(hash, camera->ViewportSize.GetX());
		MixFloat(hash, camera->ViewportSize.GetY());
		MixBits(hash, camera->Antialiasing ? 1 : 0);

		auto mixShader = [&](const std::shared_ptr<ShaderScript> &shader) {
			if (!shader) {
				MixBits(hash, 0);
				return;
			}

			MixPointer(hash, shader.get());
			MixBits(hash, shader->GetRevision());
			MixBits(hash, shader->NeedsRedrawEveryFrame() ? 1 : 0);
			for (const auto &[name, value] : shader->GetProperties()->GetParameters()) {
				MixBits(hash, std::hash<std::string>{}(name));
				MixFloat(hash, value.x);
				MixFloat(hash, value.y);
				MixFloat(hash, value.z);
				MixFloat(hash, value.w);
			}

			// Include image revisions and camera draw counts.
			for (const auto &bound : shader->GetProperties()->GetTextureSources()) {
				MixPointer(hash, bound.Image.get());
				MixBits(hash, bound.Image ? bound.Image->GetRevision() : 0);
				MixPointer(hash, bound.Camera.get());
				// Include render source kind to detect same-pointer rebinding.
				MixBits(hash, (uint64_t)bound.Render);
			}
		};

		mixShader(camera->SurfaceShader);
		for (const auto &shader : BuildShaderChain(camera)) {
			mixShader(shader);
		}

		return hash;
	}

	RenderProvider::RedrawPlan RenderProvider::PlanRedraw(DrawContext &drawContext, CameraTarget &target) {
		RedrawPlan plan;
		Camera *camera = drawContext.Camera.get();
		if (!camera) {
			return plan;
		}

		auto chain = BuildShaderChain(camera);
		size_t cut = FindCacheCut(chain);
		// Cache only when an always-redraw tail exists.
		bool hasDynamicTail = cut < chain.size();
		plan.WriteCache = hasDynamicTail;

		uint64_t cameraSignature = ComputeCameraSignature(camera);
		bool cameraMatches = camera->HasDrawn && camera->LastCameraSignature == cameraSignature;

		// Check whole scene first; walk this camera only after a global change.
		bool sceneMatches;
		if (cameraMatches && camera->LastSceneSignature == SceneSignature) {
			sceneMatches = true;
		} else {
			uint64_t visibleSignature =
				EnsureVisibleSet(camera, drawContext.WorldRoot, drawContext.LightDirection, cameraSignature)
					.Signature;
			sceneMatches = cameraMatches && camera->LastVisibleSignature == visibleSignature;
			camera->LastVisibleSignature = visibleSignature;
		}

		// History readers and redrawn inputs force a redraw.
		if (NeedsHistory.count(camera)) {
			sceneMatches = false;
		}

		// Temporal needs redraw to vary samples and converge history.
		if (GetTemporalNeeds(camera).Any()) {
			sceneMatches = false;
		}
		for (Camera *sampled : GetSampledCameras(camera)) {
			if (RedrawnThisFrame.count(sampled)) {
				sceneMatches = false;
				break;
			}
		}

		camera->LastSceneSignature = SceneSignature;
		camera->LastCameraSignature = cameraSignature;

		if (!sceneMatches) {
			// Movement resets settling and invalidates the old cache.
			camera->StillFrames = 0;
			camera->HasDrawn = true;
			plan.WriteCache = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		camera->StillFrames++;

		// Draw while settling; delay the cache copy.
		if (camera->StillFrames < CACHE_AFTER_STILL_FRAMES) {
			plan.WriteCache = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		if (!hasDynamicTail) {
			// Static complete target needs no copy or rerun.
			plan.Skip = true;
			return plan;
		}

		// Snapshot on settle; later frames run only the dynamic tail.
		if (camera->StillFrames == CACHE_AFTER_STILL_FRAMES || !target.CacheTexture) {
			plan.WriteCache = true;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		plan.RenderScene = false;
		plan.FirstShader = cut;
		plan.WriteCache = false;
		RedrawnThisFrame.insert(camera);
		return plan;
	}

	RenderProvider::CameraTarget *RenderProvider::RecordCamera(
		SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded
	) {
		outRecorded = false;

		auto *camera = drawContext.Camera.get();
		CameraTarget *target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
		if (!target) {
			return nullptr;
		}

		// Cadence skips reuse the previous target.
		if (drawContext.NotDueYet && camera->HasDrawn) {
			return target;
		}

		RedrawPlan plan = PlanRedraw(drawContext, *target);
		if (plan.Skip) {
			// Return the complete cached target without recording work.
			return target;
		}

		if (plan.WriteCache || !plan.RenderScene) {
			EnsureCacheTexture(*target);
		}

		if (plan.RenderScene) {
			if (!RecordCameraPasses(commands, drawContext, *target)) {
				return nullptr;
			}
		} else if (target->CacheTexture) {
			// Restore cached prefix, then run the dynamic tail.
			SDL_GPUBlitInfo restore{
				.source = {.texture = target->CacheTexture, .w = target->Width, .h = target->Height},
				.destination = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &restore);
		} else {
			// Missing cache falls back to a full draw.
			if (!RecordCameraPasses(commands, drawContext, *target)) {
				return nullptr;
			}
			plan.FirstShader = 0;
			plan.WriteCache = true;
		}

		RecordShaderChain(commands, camera, *target, plan.FirstShader, plan.WriteCache);
		RecordHistoryCopy(commands, camera, *target);
		// Skipped targets do not advance their draw revision.
		CountCameraDraw(camera);
		outRecorded = true;
		return target;
	}

	bool RenderProvider::RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext) {
		bool recorded = false;
		RecordCamera(commands, drawContext, recorded);
		return recorded;
	}

	void RenderProvider::EnsureCacheTexture(CameraTarget &target) {
		if (target.CacheTexture || target.Width == 0 || target.Height == 0) {
			return;
		}

		SDL_GPUTextureCreateInfo info{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = OFFSCREEN_FORMAT,
			.usage = CAMERA_TARGET_USAGE,
			.width = target.Width,
			.height = target.Height,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		target.CacheTexture = SDL_CreateGPUTexture(Gpu, &info);
	}

	void RenderProvider::DrawOffscreen(const std::vector<DrawContext> &cameras) {
		if (cameras.empty()) {
			return;
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		// One camera failure does not cancel unrelated cameras.
		bool recorded = false;
		for (const auto &drawContext : cameras) {
			DrawContext copy = drawContext;
			recorded |= RecordOffscreenCamera(commands, copy);
		}

		if (!recorded) {
			SDL_CancelGPUCommandBuffer(commands);
			return;
		}

		SubmitTracked(commands);
	}

	bool RenderProvider::RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine) {
		auto *camera = drawContext.Camera.get();

		// Script-driven renders recompute the scene hash after possible mutations.
		SceneSignature = ComputeSceneSignature(drawContext.WorldRoot, drawContext.LightDirection);

		// Record sampled cameras first in the same command buffer.
		std::vector<Camera *> roots{camera};

		// Surface cameras are dependencies even when shaders omit them.
		if (drawContext.WorldRoot) {
			for (const auto &part : drawContext.WorldRoot->Parts) {
				if (part && part->SurfaceCamera && part->SurfaceCamera.get() != camera) {
					roots.push_back(part->SurfaceCamera.get());
				}
			}
		}

		std::vector<DrawContext> dependencies;
		for (Camera *dependency : GetRenderOrder(roots)) {
			if (dependency == camera) {
				continue;
			}

			auto owned = dependency->weak_from_this().lock();
			if (!owned) {
				continue;
			}

			dependencies.push_back({
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

		// Record dependencies first so sampling sees this frame.
		for (auto &dependency : dependencies) {
			RecordOffscreenCamera(commands, dependency);
		}

		if (!RecordCameraPasses(commands, drawContext, *target)) {
			SDL_CancelGPUCommandBuffer(commands);
			SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
			return false;
		}

		// Read back the finished shader-chain output.
		RecordShaderChain(commands, camera, *target, 0, false);
		RecordHistoryCopy(commands, camera, *target);
		// Explicit renders advance the target revision.
		CountCameraDraw(camera);

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

		// Resume from a snapshot because callbacks may enqueue renders.
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

		// Shader chains require offscreen input; plain draws target the swapchain.
		bool useShaderChain = camera != nullptr && (!camera->Shaders.empty() || camera->Antialiasing);

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		if (useShaderChain) {
			// Still scenes re-blit the cached target without redrawing.
			bool recorded = false;
			CameraTarget *target = RecordCamera(commands, drawContext, recorded);
			if (!target) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}

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
			RecordWindowOverlay(commands, swapchainTexture, swapchainWidth, swapchainHeight);
			SubmitTracked(commands);
			return;
		}

		// Direct draws lack temporal buffers but must still reset stale jitter.
		if (camera) {
			camera->AdvanceJitter(GetTemporalNeeds(camera).Jitter);
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
		frameContext.SurfaceTextureSampler = PartSurfaceSampler ? PartSurfaceSampler : ShadowSampler;
		// Direct swapchain draws must compute their own visible set.
		frameContext.Visible = &EnsureVisibleSet(
			camera, drawContext.WorldRoot, drawContext.LightDirection, ComputeCameraSignature(camera)
		);

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
		RecordWindowOverlay(
			frameContext.Commands, frameContext.ColorTarget, frameContext.Width, frameContext.Height
		);

		SubmitTracked(frameContext.Commands);
	}

	void RenderProvider::Resize(int width, int height) {
		if (width < 1 || height < 1) {
			return;
		}
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
