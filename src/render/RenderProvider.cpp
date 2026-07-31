
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
#include "gargantuan/render/Frustum.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/SceneHash.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/render/ShaderReflection.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

	void RenderProvider::UpdateSceneSignature(const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection) {
		SceneDrawIndex.SceneSignature =
			SceneDrawIndex.SyncAndComputeSceneSignature(world, lightDirection, CameraTextureGeneration);
	}

	const std::vector<Camera *> &RenderProvider::GetSurfaceCameras() const {
		return SceneDrawIndex.SurfaceCameras;
	}

	RenderProvider::RenderProvider(SDL_Window *window, SDL_GPUDevice *gpu) : Window(window), Gpu(gpu) {
		if (!SDL_ClaimWindowForGPUDevice(Gpu, Window)) {
			SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
			std::abort();
		}

		SwapchainFormat = SDL_GetGPUSwapchainTextureFormat(Gpu, Window);

		SDL_GPUTextureCreateInfo shadowMapTextureInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
			.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
			.width = 2048,
			.height = 2048,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		ShadowMapTexture = SDL_CreateGPUTexture(gpu, &shadowMapTextureInfo);

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
		IsVelocityPassUsedThisFrame = false;
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

		// After all cameras, for the same reason: the deltas are a property of the
		// frame, not of a camera, and every camera this frame drew from the buffer
		// they were uploaded into.
		SceneDrawIndex.DirtyDrawSlots.clear();
		SceneDrawIndex.DrawInstancesAllDirty = false;

		if (!FrameFences.empty()) {
			FramesInFlight.push_back(std::move(FrameFences));
			FrameFences.clear();
		}
	}

	void RenderProvider::SubmitAndTrackFence(SDL_GPUCommandBuffer *commands) {
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

		// Each of these frees what its own file allocates.
		ReleaseReadbacks();
		ReleaseTextureUploads();
		ReleaseCameraResources();
		ReleaseShaderCache();
		ReleaseWindowOverlay();

		if (FullscreenVertexShader) {
			SDL_ReleaseGPUShader(Gpu, FullscreenVertexShader);
			FullscreenVertexShader = nullptr;
		}

		if (OpaqueVertexShader) {
			SDL_ReleaseGPUShader(Gpu, OpaqueVertexShader);
			OpaqueVertexShader = nullptr;
		}

		if (DepthTexture) {
			SDL_ReleaseGPUTexture(Gpu, DepthTexture);
			DepthTexture = nullptr;
		}

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
		std::vector<DrawContext> ordered;
		{
			G_PROFILE("Pane Order");
			std::vector<Camera *> roots;
			roots.reserve(cameras.size());
			for (const auto &drawContext : cameras) {
				roots.push_back(drawContext.Camera.get());
			}

			std::unordered_map<Camera *, const DrawContext *> byCamera;
			for (const auto &drawContext : cameras) {
				byCamera[drawContext.Camera.get()] = &drawContext;
			}

			for (Camera *camera : GetRenderOrder(roots)) {
				auto it = byCamera.find(camera);
				if (it != byCamera.end()) {
					ordered.push_back(*it->second);
				}
			}
		}

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(Gpu);
		if (!commands) {
			SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
			return;
		}

		// Draw offscreen because swapchain textures cannot be sampled or blitted from.
		std::vector<std::pair<const CameraTextureSet *, const Camera *>> recordedPanes;
		{
			G_PROFILE("Record Panes");
			for (const auto &drawContext : ordered) {
				auto *camera = drawContext.Camera.get();

				// Reuse a still pane's prior target.
				DrawContext mutableDrawContext = drawContext;
				bool recorded = false;
				CameraTextureSet *target = RecordCamera(commands, mutableDrawContext, recorded);
				if (!target) {
					continue;
				}
				recordedPanes.emplace_back(target, camera);
			}
		}

		SDL_GPUTexture *swapchainTexture = nullptr;
		uint32_t windowWidth = 0, windowHeight = 0;
		{
			// Blocks until the compositor hands a buffer back
			G_PROFILE("Swapchain");
			if (!SDL_AcquireGPUSwapchainTexture(commands, Window, &swapchainTexture, &windowWidth, &windowHeight) ||
				!swapchainTexture) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}
		}

		G_PROFILE("Present");
		bool first = true;
		for (const auto &[target, camera] : recordedPanes) {
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
		SubmitAndTrackFence(commands);
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

	RenderProvider::RedrawPlan RenderProvider::PlanRedraw(DrawContext &drawContext, CameraTextureSet &target) {
		RedrawPlan plan;
		Camera *camera = drawContext.Camera.get();
		if (!camera) {
			return plan;
		}

		auto chain = BuildShaderChain(camera);
		size_t cut = FindFirstAlwaysRedrawShaderIndex(chain);
		// Cache only when an always-redraw tail exists.
		bool hasDynamicTail = cut < chain.size();
		plan.ShouldWriteCacheTexture = hasDynamicTail;

		uint64_t cameraSignature = ComputeCameraSignature(camera);
		bool cameraMatches = camera->HasDrawn && camera->LastCameraSignature == cameraSignature;

		// Check whole scene first; walk this camera only after a global change.
		bool sceneMatches;
		if (cameraMatches && camera->LastSceneSignature == SceneDrawIndex.SceneSignature) {
			sceneMatches = true;
		} else if (cameraMatches) {
			uint64_t visiblePartsHash =
				SceneDrawIndex.EnsureVisibleSet(camera, drawContext.WorldRoot, drawContext.LightDirection, cameraSignature, true)
					.VisiblePartsHash;
			sceneMatches = camera->LastVisiblePartsHashValid && camera->LastVisiblePartsHash == visiblePartsHash;
			camera->LastVisiblePartsHash = visiblePartsHash;
			camera->LastVisiblePartsHashValid = true;
		} else {
			// A camera that moved is redrawing whatever the signature says, so
			// the phase that computes it is skipped and its answer disowned.
			SceneDrawIndex.EnsureVisibleSet(camera, drawContext.WorldRoot, drawContext.LightDirection, cameraSignature);
			sceneMatches = false;
			camera->LastVisiblePartsHashValid = false;
		}

		// History readers and redrawn inputs force a redraw.
		if (CamerasNeedingHistory.count(camera)) {
			sceneMatches = false;
		}

		// Temporal needs redraw to vary samples and converge history.
		if (GetTemporalNeeds(camera).Any()) {
			sceneMatches = false;
		}
		for (Camera *sampled : GetDirectlySampledCameras(camera)) {
			if (RedrawnThisFrame.count(sampled)) {
				sceneMatches = false;
				break;
			}
		}

		camera->LastSceneSignature = SceneDrawIndex.SceneSignature;
		camera->LastCameraSignature = cameraSignature;

		if (!sceneMatches) {
			// Movement resets settling and invalidates the old cache.
			camera->StillFrames = 0;
			camera->HasDrawn = true;
			plan.ShouldWriteCacheTexture = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		camera->StillFrames++;

		// Draw while settling; delay the cache copy.
		if (camera->StillFrames < CACHE_AFTER_STILL_FRAMES) {
			plan.ShouldWriteCacheTexture = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		if (!hasDynamicTail) {
			// Static complete target needs no copy or rerun.
			plan.ShouldSkipCamera = true;
			return plan;
		}

		// Snapshot on settle; later frames run only the dynamic tail.
		if (camera->StillFrames == CACHE_AFTER_STILL_FRAMES || !target.CachedChainPrefixTexture) {
			plan.ShouldWriteCacheTexture = true;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		plan.RenderScene = false;
		plan.FirstShaderChainIndex = cut;
		plan.ShouldWriteCacheTexture = false;
		RedrawnThisFrame.insert(camera);
		return plan;
	}

	RenderProvider::CameraTextureSet *RenderProvider::RecordCamera(
		SDL_GPUCommandBuffer *commands, DrawContext &drawContext, bool &outRecorded
	) {
		outRecorded = false;

		auto *camera = drawContext.Camera.get();
		CameraTextureSet *target = nullptr;
		{
			G_PROFILE("Camera Target");
			target = AcquireCameraTarget(camera, camera && (!camera->Shaders.empty() || camera->Antialiasing));
		}
		if (!target) {
			return nullptr;
		}

		// Cadence skips reuse the previous target.
		if (drawContext.ShouldSkipRedraw && camera->HasDrawn) {
			return target;
		}

		RedrawPlan plan;
		{
			G_PROFILE("Plan Redraw");
			plan = PlanRedraw(drawContext, *target);
		}
		if (plan.ShouldSkipCamera) {
			return target;
		}

		if (plan.ShouldWriteCacheTexture || !plan.RenderScene) {
			EnsureCacheTexture(*target);
		}

		if (plan.RenderScene) {
			if (!RecordCameraPasses(commands, drawContext, *target)) {
				return nullptr;
			}
		} else if (target->CachedChainPrefixTexture) {
			// Restore cached prefix, then run the dynamic tail.
			SDL_GPUBlitInfo restore{
				.source = {.texture = target->CachedChainPrefixTexture, .w = target->Width, .h = target->Height},
				.destination = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &restore);
		} else {
			if (!RecordCameraPasses(commands, drawContext, *target)) {
				return nullptr;
			}
			plan.FirstShaderChainIndex = 0;
			plan.ShouldWriteCacheTexture = true;
		}

		RecordShaderChain(commands, camera, *target, plan.FirstShaderChainIndex, plan.ShouldWriteCacheTexture);
		{
			G_PROFILE("History Copy");
			RecordTemporalHistoryCopies(commands, camera, *target);
		}
		// Skipped targets do not advance their draw revision.
		SceneDrawIndex.CountCameraDraw(camera);
		outRecorded = true;
		return target;
	}

	bool RenderProvider::RecordOffscreenCamera(SDL_GPUCommandBuffer *commands, DrawContext &drawContext) {
		bool recorded = false;
		RecordCamera(commands, drawContext, recorded);
		return recorded;
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
			DrawContext mutableDrawContext = drawContext;
			recorded |= RecordOffscreenCamera(commands, mutableDrawContext);
		}

		if (!recorded) {
			SDL_CancelGPUCommandBuffer(commands);
			return;
		}

		SubmitAndTrackFence(commands);
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
			bool recorded = false;
			CameraTextureSet *target = RecordCamera(commands, drawContext, recorded);
			if (!target) {
				SDL_CancelGPUCommandBuffer(commands);
				return;
			}

			SDL_GPUTexture *swapchainTexture = nullptr;
			uint32_t swapchainWidth = 0, swapchainHeight = 0;
			{
				// Blocks until the compositor hands a buffer back
				G_PROFILE("Swapchain");
				if (!SDL_AcquireGPUSwapchainTexture(
						commands, Window, &swapchainTexture, &swapchainWidth, &swapchainHeight
					) ||
					!swapchainTexture) {
					SDL_CancelGPUCommandBuffer(commands);
					return;
				}
			}

			G_PROFILE("Present");
			SDL_GPUBlitInfo blit{
				.source = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.destination = {.texture = swapchainTexture, .w = swapchainWidth, .h = swapchainHeight},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_LINEAR,
			};
			SDL_BlitGPUTexture(commands, &blit);
			RecordWindowOverlay(commands, swapchainTexture, swapchainWidth, swapchainHeight);
			SubmitAndTrackFence(commands);
			return;
		}

		// Direct draws lack temporal buffers but must still reset stale jitter.
		if (camera) {
			camera->AdvanceJitter(GetTemporalNeeds(camera).Jitter);
		}

		FrameContext frameContext;
		frameContext.Commands = commands;
		BindSceneToFrame(frameContext, drawContext);

		if (DepthTexture) {
			frameContext.DepthTexture = DepthTexture;
		} else {
			SDL_CancelGPUCommandBuffer(frameContext.Commands);
			return;
		}

		auto swapchainResult = SDL_AcquireGPUSwapchainTexture(
			frameContext.Commands, Window, &frameContext.ColorTargetTexture, &frameContext.Width, &frameContext.Height
		);
		if (!swapchainResult) {
			SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
			if (frameContext.Commands) {
				SDL_CancelGPUCommandBuffer(frameContext.Commands);
			};
			return;
		}

		if (!frameContext.Commands || !frameContext.ColorTargetTexture || !frameContext.DepthTexture ||
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
			frameContext.Commands, frameContext.ColorTargetTexture, frameContext.Width, frameContext.Height
		);

		SubmitAndTrackFence(frameContext.Commands);
	}

	void RenderProvider::Resize(int width, int height) {
		if (width < 1 || height < 1) {
			return;
		}
		SDL_SetGPUSwapchainParameters(
			Gpu,
			Window,
			SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
			ShouldPresentUncapped ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_VSYNC
		);

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
