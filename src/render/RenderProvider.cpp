// #define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/render/RenderPass.hpp"
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
			if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);
		}
		CameraTargets.clear();

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
		if (it->second.DepthTexture) SDL_ReleaseGPUTexture(Gpu, it->second.DepthTexture);
		CameraTargets.erase(it);
	}

	RenderProvider::CameraTarget *RenderProvider::AcquireCameraTarget(Camera *camera) {
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
		if (target.ColorTexture != nullptr && target.Width == width && target.Height == height) {
			return &target;
		}

		// The viewport changed, so the old textures are the wrong size
		if (target.ColorTexture) SDL_ReleaseGPUTexture(Gpu, target.ColorTexture);
		if (target.DepthTexture) SDL_ReleaseGPUTexture(Gpu, target.DepthTexture);

		SDL_GPUTextureCreateInfo colorInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = OFFSCREEN_FORMAT,
			.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
			.width = width,
			.height = height,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		target.ColorTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);

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
		CameraTarget *target = AcquireCameraTarget(camera);
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

		SDL_SubmitGPUCommandBuffer(commands);
	}

	bool RenderProvider::RequestRender(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine) {
		auto *camera = drawContext.Camera.get();
		CameraTarget *target = AcquireCameraTarget(camera);
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
		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
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
