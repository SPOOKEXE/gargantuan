// A Camera:Render() in flight. The draw goes out with a fence and a download
// buffer, and the Luau thread that asked for it is parked until the GPU
// signals -- so the pixels never come back inside the frame that asked.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace gargantuan {
	bool RenderProvider::BeginRenderReadback(DrawContext drawContext, lua_State *thread, ThreadEngine *threadEngine) {
		auto *camera = drawContext.Camera.get();

		// Script-driven renders recompute the scene hash after possible mutations.
		UpdateSceneSignature(drawContext.WorldRoot, drawContext.LightDirection);

		// Record sampled cameras first in the same command buffer.
		std::vector<Camera *> roots{camera};

		// Surface cameras are dependencies even when shaders omit them.
		if (drawContext.WorldRoot) {
			for (const auto &part : drawContext.WorldRoot->Parts) {
				if (part && part->GetSurfaceOrDefault().Camera && part->GetSurfaceOrDefault().Camera.get() != camera) {
					roots.push_back(part->GetSurfaceOrDefault().Camera.get());
				}
			}
		}

		std::vector<DrawContext> dependencies;
		for (Camera *dependency : GetRenderOrder(roots)) {
			if (dependency == camera) {
				continue;
			}

			auto ownedDependencyCamera = dependency->weak_from_this().lock();
			if (!ownedDependencyCamera) {
				continue;
			}

			dependencies.push_back({
				.WorldRoot = drawContext.WorldRoot,
				.Camera = std::static_pointer_cast<Camera>(ownedDependencyCamera),
				.LightDirection = drawContext.LightDirection,
			});
		}

		CameraTextureSet *target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
		if (!target || !threadEngine) {
			return false;
		}

		uint32_t width = target->Width;
		uint32_t height = target->Height;
		uint32_t downloadByteCount = width * height * EditableImage::CHANNELS;

		SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
			.size = downloadByteCount,
		};
		SDL_GPUTransferBuffer *transferBuffer = SDL_CreateGPUTransferBuffer(Gpu, &transferInfo);
		if (!transferBuffer) {
			SDL_Log("Failed to create a %u byte readback buffer: %s", downloadByteCount, SDL_GetError());
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
		RecordTemporalHistoryCopies(commands, camera, *target);
		SceneDrawIndex.CountCameraDraw(camera);

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
			.ParkedThread = thread,
			.ThreadReference = threadEngine->TakeThreadReference(thread),
			.Fence = fence,
			.ReadbackTransferBuffer = transferBuffer,
			.Width = width,
			.Height = height,
			.Image = image,
		});

		return true;
	}

	void RenderProvider::ResumeCompletedReadbacks(ThreadEngine *threadEngine) {
		if (PendingRenders.empty() || !threadEngine) {
			return;
		}

		// Resume from a snapshot because callbacks may enqueue renders.
		std::vector<PendingRender> stillPending;
		std::vector<PendingRender> signalledReadbacks;

		for (auto &pending : PendingRenders) {
			if (SDL_QueryGPUFence(Gpu, pending.Fence)) {
				signalledReadbacks.push_back(pending);
			} else {
				stillPending.push_back(pending);
			}
		}

		PendingRenders = std::move(stillPending);

		for (auto &pending : signalledReadbacks) {
			auto *mappedPixels =
				static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(Gpu, pending.ReadbackTransferBuffer, false));
			if (mappedPixels) {
				pending.Image->SetPixels((int)pending.Width, (int)pending.Height, mappedPixels);
				SDL_UnmapGPUTransferBuffer(Gpu, pending.ReadbackTransferBuffer);
			} else {
				SDL_Log("Failed to map a readback buffer: %s", SDL_GetError());
				pending.Image->SetPixels((int)pending.Width, (int)pending.Height, nullptr);
			}

			SDL_ReleaseGPUFence(Gpu, pending.Fence);
			SDL_ReleaseGPUTransferBuffer(Gpu, pending.ReadbackTransferBuffer);

			StackValue<Instance::Pointer>::Push(pending.ParkedThread, pending.Image);
			threadEngine->ResumeThread(pending.ParkedThread, pending.ThreadReference, 1);
		}
	}

	// A parked thread cannot be resumed after teardown, so the fence and its
	// buffer go and the thread stays parked -- the process is going down anyway.
	void RenderProvider::ReleaseReadbacks() {
		for (auto &pending : PendingRenders) {
			if (pending.Fence) SDL_ReleaseGPUFence(Gpu, pending.Fence);
			if (pending.ReadbackTransferBuffer) SDL_ReleaseGPUTransferBuffer(Gpu, pending.ReadbackTransferBuffer);
		}
		PendingRenders.clear();
	}
}
