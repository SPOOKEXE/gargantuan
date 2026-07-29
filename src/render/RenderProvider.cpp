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
		// Who followed whom is a fact about one frame only
		RedrawnThisFrame.clear();
		// Re-answered by whichever cameras draw motion vectors this frame
		VelocityInUse = false;
		// Only moves here, so everything recorded between this and EndFrame
		// counts as the same frame and is safe from eviction
		FrameIndex++;

		// Zero would mean nothing is ever waited on, which is the unbounded
		// backlog this whole mechanism exists to stop
		size_t maximum = (size_t)glm::max(maximumFramesInFlight, 1);
		while (FramesInFlight.size() >= maximum) {
			RetireFrame(FramesInFlight.front());
			FramesInFlight.pop_front();
		}
	}

	void RenderProvider::EndFrame() {
		// Every camera has had its turn, so this frame's positions are now the
		// previous ones. Doing it here rather than as each camera draws is what
		// makes them all measure motion against the same frame.
		StampPreviousTransforms();

		if (!FrameFences.empty()) {
			FramesInFlight.push_back(std::move(FrameFences));
			FrameFences.clear();
		}
	}

	void RenderProvider::SubmitTracked(SDL_GPUCommandBuffer *commands) {
		// Falling back to a plain submit keeps the picture correct if a fence
		// cannot be had; only the pacing is lost
		if (SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands)) {
			FrameFences.push_back(fence);
		}
	}

	void RenderProvider::Destroy() {
		SDL_WaitForGPUIdle(Gpu);

		// The GPU is idle, so every fence has signalled and only needs releasing
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
		// Only ever built if a camera asked for motion vectors
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
		if (target.VelocityTexture) SDL_ReleaseGPUTexture(Gpu, target.VelocityTexture);
		if (target.ViewDepthTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthTexture);
		if (target.ViewDepthHistoryTexture) SDL_ReleaseGPUTexture(Gpu, target.ViewDepthHistoryTexture);
		// A resize invalidates the cached image along with everything else
		if (target.CacheTexture) SDL_ReleaseGPUTexture(Gpu, target.CacheTexture);
		target.ScratchTexture = nullptr;
		target.HistoryTexture = nullptr;
		target.VelocityTexture = nullptr;
		target.ViewDepthTexture = nullptr;
		target.ViewDepthHistoryTexture = nullptr;
		target.CacheTexture = nullptr;
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

		// One of the reader's own buffers. No camera is named because none can
		// be: the antialias pass is a single script shared by every camera, so
		// the only camera it can mean is whichever one it is running on.
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

	SDL_GPUSampler *RenderProvider::GetSourceSampler(const ShaderScript::TextureSource &source) {
		// Averaging two neighbouring measurements invents a third that neither
		// surface reported: half a step neither took, or a distance at which
		// nothing stands. A pass reprojecting by the one or comparing against
		// the other is then reasoning about something that was never there.
		// Only the pictures are smoothed.
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

			for (const auto &source : shader->GetTextureSources()) {
				switch (source.Render) {
				case Enums::RenderTexture::History:
					needs.History = true;
					break;
				// All three come out of the one geometry pass, and the copy is
				// of what that pass wrote, so asking for the copy asks for the
				// pass as well
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

		// The antialias pass is in here, which is the whole point: swapping in a
		// temporal one through RenderSettings is what turns these on
		for (const auto &shader : BuildShaderChain(camera)) {
			consider(shader);
		}
		// A surface shader can ask too. It shades during the opaque pass, which
		// is why the velocity pass is recorded ahead of that one rather than
		// after it -- otherwise the vectors it read would be a frame old.
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
			// RecordHistoryCopy is what keeps it current; this is only about it
			// existing before the chain first samples it
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

				// Cleared, because a pass is about to read it a moment before
				// the frame that fills it. Whatever the driver handed back is
				// not a picture and blending against it shows; black is at
				// least defined, and a pass rejecting history on how far it
				// sits from its surroundings throws it out on the first frame
				// anyway, which is exactly what should happen to a history that
				// does not exist yet.
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

		// Written and read, never drawn into by a shader chain, so they want
		// less than a camera's own picture does
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

		// Both, whichever was asked for: one pass writes them together, and a
		// render pass has to be given every attachment its pipeline declares
		if (needs.Motion) {
			measurement(target.VelocityTexture, VELOCITY_FORMAT, "motion vector");
			measurement(target.ViewDepthTexture, VIEW_DEPTH_FORMAT, "view depth");
		}

		if (needs.DepthHistory && !target.ViewDepthHistoryTexture) {
			measurement(target.ViewDepthHistoryTexture, VIEW_DEPTH_FORMAT, "previous view depth");

			// Cleared to the far plane, like the buffer it is a copy of, for
			// the one frame it is read before it has ever been written. A pass
			// comparing against it then sees empty space everywhere rather than
			// a wall of geometry pressed against the lens.
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
			// Nothing wanted motion vectors this frame. What is being carried
			// belongs to whenever the last camera stopped asking, so drop it:
			// a camera that starts asking again should read no motion for a
			// frame rather than the distance everything moved in between.
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

		// This frame's distances become last frame's, taken here so that the
		// chain that just ran read the copy from before it. Ahead of the colour
		// copy for no reason but that the two are independent.
		if (needs.DepthHistory && target.ViewDepthTexture && target.ViewDepthHistoryTexture) {
			SDL_GPUBlitInfo blit{
				.source = {.texture = target.ViewDepthTexture, .w = target.Width, .h = target.Height},
				.destination = {.texture = target.ViewDepthHistoryTexture, .w = target.Width, .h = target.Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &blit);
		}

		// Two reasons to keep a picture, and they expire differently. Being part
		// of a camera loop is remembered in NeedsHistory, which is only cleared
		// when the camera goes away. A pass asking for
		// Enum.RenderTexture.History is asked about again every frame, so
		// putting the engine's own antialias pass back stops the copy the same
		// frame rather than leaving the camera paying for one nothing reads.
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
		PartTextures.clear();
		if (!worldRoot) {
			return;
		}

		for (const auto &part : worldRoot->Parts) {
			if (!part) {
				continue;
			}

			// A live camera feed is the more specific of the two, so it wins
			// when a part somehow carries both
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

		// Revisions only ever go up, so nothing can ask for the old one again.
		// Both the plain entry and the per-format surface ones belong to it.
		std::string stem = "code:" + std::to_string(serial) + ":" + std::to_string(it->second);
		for (auto entry = ShaderCache.begin(); entry != ShaderCache.end();) {
			if (entry->first.rfind(stem, 0) != 0) {
				++entry;
				continue;
			}

			// Recompiling partway through a frame would otherwise release a
			// pipeline already bound into a command buffer waiting to be
			// submitted. Left behind, it ages out through the trim instead.
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
				// Anything already handed out this frame may be bound into a
				// command buffer that has not been submitted, and releasing it
				// would leave that binding pointing at nothing
				if (compiled.LastUsedFrame == FrameIndex) {
					continue;
				}

				if (!oldestKey || compiled.LastUsedFrame < oldestFrame) {
					oldestKey = &key;
					oldestFrame = compiled.LastUsedFrame;
				}
			}

			// Everything left belongs to this frame, so the bound gives way
			// until the next one rather than the picture doing so
			if (!oldestKey) {
				return;
			}

			ReleaseCachedShader(*oldestKey);
		}
	}

	RenderProvider::CompiledShader &RenderProvider::InsertCachedShader(
		const std::string &key, ShaderScript *shader
	) {
		// Erases, so it runs before the reference the caller is about to hold
		// is taken
		DropSupersededShader(shader);

		CompiledShader &compiled = ShaderCache[key];
		compiled.LastUsedFrame = FrameIndex;

		// Trimming afterwards is what makes the bound the real ceiling rather
		// than one below it. Safe because this entry is stamped with the
		// current frame, so the trim will not pick it, and erasing any other
		// entry leaves the reference alone.
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

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->Failed ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
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

		// The built-in antialias pass runs last, after whatever the camera's
		// own chain did
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
		// Nothing animates, so the whole chain is cacheable
		return chain.size();
	}

	void RenderProvider::RecordShaderChain(
		SDL_GPUCommandBuffer *commands, Camera *camera, CameraTarget &target, size_t firstShader, bool writeCache
	) {
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

		// The chain bounces between the two textures; `source` always holds
		// what has been produced so far
		SDL_GPUTexture *source = target.ColorTexture;
		SDL_GPUTexture *destination = target.ScratchTexture;

		for (size_t index = firstShader; index < chain.size(); index++) {
			// Snapshot what the cacheable half produced, just before the first
			// pass that has to run again every frame
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

				for (auto &bound : post->GetTextureSources()) {
					SDL_GPUTexture *texture = ResolveTextureSource(camera, bound);
					if (!texture || bindingCount > ShaderScript::MAXIMUM_IMAGES) {
						continue;
					}
					bindings[bindingCount++] = {.texture = texture, .sampler = GetSourceSampler(bound)};
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

		// Before the bindings are built rather than after: they are what needs
		// it, and creating it below meant the first surface shader to bind an
		// image bound it with no sampler at all
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

		// Slot 0 is always the shadow map the engine's vertex stage set up
		samplerStorage.clear();
		samplerStorage.push_back({.texture = frameContext.ShadowMapTexture, .sampler = ShadowSampler});

		for (auto &source : camera->SurfaceShader->GetTextureSources()) {
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
		if (!commands || !target.ColorTexture || !target.DepthTexture || !ShadowMapTexture) {
			return false;
		}

		Camera *camera = drawContext.Camera.get();
		TemporalNeeds needs = GetTemporalNeeds(camera);
		if (camera) {
			EnsureTemporalTargets(commands, camera, CameraTargets[camera], needs);
			// Only here, where the world is about to be drawn again. A camera
			// the engine skipped keeps the offset its picture was drawn with,
			// so the offset and the pixels always describe each other.
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
		// Both or neither: the pass declares two attachments and a render pass
		// must be handed every one of them
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
		// Usually the walk PlanRedraw already made. The readback path reaches
		// here without one, so this is where it is guaranteed rather than
		// assumed.
		frameContext.Visible = &EnsureVisibleSet(
			drawContext.Camera.get(),
			drawContext.WorldRoot,
			drawContext.LightDirection,
			ComputeCameraSignature(drawContext.Camera.get())
		);

		// A camera's SurfaceShader replaces the opaque pass's fragment stage
		std::vector<uint8_t> surfaceParameters;
		std::vector<SDL_GPUTextureSamplerBinding> surfaceSamplers;
		PrepareSurfaceShader(
			frameContext, drawContext.Camera.get(), OFFSCREEN_FORMAT, surfaceParameters, surfaceSamplers
		);

		// The shadow map only depends on the light, but it has to be recorded
		// into this command buffer for the opaque pass to sample it
		SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));

		// Ahead of the opaque pass, so a SurfaceShader that binds the motion
		// vectors reads this frame's rather than the last one's. It clears the
		// depth buffer and throws it away again, which costs nothing the opaque
		// pass was going to keep -- that one clears depth for itself.
		if (frameContext.VelocityTarget) {
			if (RenderPass *velocity = GetVelocityPass()) {
				SDL_EndGPURenderPass(velocity->Draw(Gpu, frameContext));
				VelocityInUse = true;
			}
		}

		SDL_EndGPURenderPass(OffscreenOpaquePass->Draw(Gpu, frameContext));

		// What this camera drew through, for the next frame's motion vectors to
		// measure against. Unjittered: the offset describes the sampling, not
		// where the camera is.
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
		// A swapped-in pass stands in for the built-in entirely. The built-in
		// is kept rather than dropped, so turning the swap off again does not
		// have to rebuild it.
		if (AntialiasOverride) {
			return AntialiasOverride;
		}

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

			// Same as the single-camera window path: a pane whose corner of the
			// world has not moved is blitted from what it drew last time
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
				// The first blit clears whatever the window held; the rest must
				// not wipe the panes drawn before them
				.load_op = first ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
				.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
				.filter = SDL_GPU_FILTER_LINEAR,
			};
			SDL_BlitGPUTexture(commands, &blit);
			first = false;
		}

		SubmitTracked(commands);
	}

	namespace {
		// FNV-1a, mixed a word at a time. Only ever compared against itself, so
		// a collision costs a skipped redraw for one frame, not correctness
		// across the board -- and 64 bits makes that vanishingly unlikely.
		inline void MixBits(uint64_t &hash, uint64_t value) {
			hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
		}

		inline void MixFloat(uint64_t &hash, float value) {
			// Through the bit pattern, so -0.0 and 0.0 are the same and a NaN
			// is at least stable rather than never equal to itself
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

		// The four side planes of a camera's frustum in world space, each held
		// as (normal, distance) with the inside on the positive side
		struct SidePlanes {
			glm::vec4 Planes[4];
		};

		// Near and far are deliberately left out. Near is 0.1 and far is
		// 100000, so neither culls anything worth culling, and leaving them
		// out means only rows 0, 1 and 3 of the matrix are read. Those are the
		// same whether the projection maps depth to 0..1 or to -1..1, so this
		// does not care which convention glm was built with -- one less thing
		// to get quietly wrong.
		//
		// The four alone still reject everything behind the camera: they meet
		// at the eye, and the pyramid running backwards from it fails all four.
		SidePlanes ExtractSidePlanes(const glm::mat4 &viewProjection) {
			// glm is column major, so a row is the nth component of each column
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

			// Normalised, so a plane distance comes out in world units and can
			// be compared against a radius
			for (auto &plane : planes.Planes) {
				float length = glm::length(glm::vec3(plane));
				if (length > 0.0f) {
					plane /= length;
				}
			}
			return planes;
		}

		// Whether the segment from `from` to `to`, fattened by `radius`, is
		// anywhere on the inside. A capsule rather than a sphere because a
		// part throws its shadow along a line, and that line has to be tested
		// too; passing the same point twice makes it a plain sphere test.
		bool CapsuleInside(const SidePlanes &planes, glm::vec3 from, glm::vec3 to, float radius) {
			for (const auto &plane : planes.Planes) {
				glm::vec3 normal(plane);
				// Out only when both ends are out, or a capsule lying across
				// the frustum would be thrown away by the plane each end
				// happens to be behind
				if (glm::dot(normal, from) + plane.w < -radius && glm::dot(normal, to) + plane.w < -radius) {
					return false;
				}
			}
			return true;
		}

		// How far a shadow can reach past the part throwing it. ShadowPass
		// renders into an orthographic box 200 units deep, so nothing can be
		// projected further than that; a part further from the frustum than
		// this cannot darken anything inside it.
		//
		// Without this a part stepping out of view would stop counting as
		// changed, and the shadow it was casting into the view would freeze
		// where it stood.
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

		// The count matters on its own: a part appearing and another vanishing
		// in the same frame would otherwise leave the rest hashing identically
		MixBits(hash, world->Parts.size());

		for (const auto &part : world->Parts) {
			if (!part) {
				MixBits(hash, 0);
				continue;
			}

			// Two words per part, and neither of them touches a property. The
			// QuickHash says whether anything about the part was written since
			// last frame; the pointer catches one part being swapped for
			// another. Reading every transform and colour instead would make
			// this cost scale with how much a part has rather than with how
			// many there are.
			MixPointer(hash, part.get());
			MixBits(hash, part->QuickHash);
			// A part showing a camera changes when that camera does, and one
			// showing an image when the image is drawn into. Both need the
			// count or the revision as well as the pointer: the pointer only
			// says which one is being shown, and a screen showing the same
			// camera it showed last frame is the ordinary case, not the still
			// one.
			MixPointer(hash, part->SurfaceCamera.get());
			MixBits(hash, GetCameraDrawCount(part->SurfaceCamera.get()));
			MixPointer(hash, part->SurfaceImage.get());
			MixBits(hash, part->SurfaceImage ? part->SurfaceImage->GetRevision() : 0);
		}

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
		out.InView.clear();
		out.ShadowsIntoView.clear();

		uint64_t hash = 0xCBF29CE484222325ull;
		MixVec3(hash, lightDirection);

		if (!camera || !world) {
			out.Signature = hash;
			return;
		}

		SidePlanes planes = ExtractSidePlanes(camera->GetProjectionMatrix() * camera->GetViewMatrix());
		// LightDirection points towards the light, so a shadow falls the other
		// way: from the part, away from the light
		glm::vec3 shadowStep = -glm::normalize(lightDirection) * SHADOW_CAST_REACH;

		uint64_t visible = 0;
		for (const auto &part : world->Parts) {
			if (!part) {
				continue;
			}

			// Half the box diagonal, so the sphere holds the part whichever way
			// it is turned. Loose, which is the safe way round: too big only
			// costs a redraw, too small drops something that was on screen.
			float radius = glm::length(part->Size) * 0.5f;
			glm::vec3 centre = part->CFrame.Position;

			// The sphere on its own, which is the capsule test with no sweep:
			// what the opaque pass would actually put on screen
			bool inView = CapsuleInside(planes, centre, centre, radius);
			// The same sphere swept along the shadow, so a caster off the side
			// of the screen still counts. Only worth asking of a part that
			// casts at all.
			bool shadowReaches =
				part->CastShadow && CapsuleInside(planes, centre, centre + shadowStep, radius);

			if (inView) {
				out.InView.insert(part.get());
			}
			// A caster already on screen throws its shadow onto the screen too,
			// so this set is the wider of the two and never drops one InView
			// holds. The sweep starts at the part, so inView implies it, but
			// saying so costs nothing and does not rely on noticing that.
			if (part->CastShadow && (inView || shadowReaches)) {
				out.ShadowsIntoView.insert(part.get());
			}

			// Anything that can change the picture: by being in it, or by
			// darkening something that is
			if (!inView && !shadowReaches) {
				continue;
			}

			visible++;
			MixPointer(hash, part.get());
			MixBits(hash, part->QuickHash);
			// A screen in view is a reason to redraw whenever the camera on it
			// has drawn again, however still the screen itself has been. This
			// is the narrow check, so it asks only about the ones this camera
			// can actually see: a monitor behind it costs it nothing.
			MixPointer(hash, part->SurfaceCamera.get());
			MixBits(hash, GetCameraDrawCount(part->SurfaceCamera.get()));
			MixPointer(hash, part->SurfaceImage.get());
			MixBits(hash, part->SurfaceImage ? part->SurfaceImage->GetRevision() : 0);
		}

		// Cheap insurance on how many were in view, since the loop above
		// contributes nothing at all for a frame where none of them are
		MixBits(hash, visible);
		out.Signature = hash;
	}

	const VisibleSet &RenderProvider::EnsureVisibleSet(
		Camera *camera, const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t cameraSignature
	) {
		VisibleSet &set = VisibleSets[camera];

		// The walk depends on the world and on where the camera is pointing,
		// and on nothing else. While neither has moved the previous answer is
		// still the right one, so the redraw check and the passes share a
		// single walk rather than taking one each.
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
			for (const auto &[name, value] : shader->GetParameters()) {
				MixBits(hash, std::hash<std::string>{}(name));
				MixFloat(hash, value.x);
				MixFloat(hash, value.y);
				MixFloat(hash, value.z);
				MixFloat(hash, value.w);
			}

			// A bound image changes the picture when it is drawn into, and a
			// bound camera when it redraws
			for (const auto &bound : shader->GetTextureSources()) {
				MixPointer(hash, bound.Image.get());
				MixBits(hash, bound.Image ? bound.Image->GetRevision() : 0);
				MixPointer(hash, bound.Camera.get());
				// Rebinding a slot from an image to the camera's own history
				// changes what the pass reads without changing anything above
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
		// Only worth keeping a cache when something after it has to rerun
		bool hasDynamicTail = cut < chain.size();
		plan.WriteCache = hasDynamicTail;

		uint64_t cameraSignature = ComputeCameraSignature(camera);
		bool cameraMatches = camera->HasDrawn && camera->LastCameraSignature == cameraSignature;

		// Two checks, wide then narrow. The wide one is already computed and
		// costs a comparison: when nothing anywhere moved and the camera did
		// not either, the subset this camera can see cannot have changed and
		// there is no reason to work out what that subset is. Only when
		// something did move is the frustum walked, and then the answer is
		// about this camera alone -- a part shuffling about behind it, or off
		// the side of the screen, leaves its picture exactly as it was.
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

		// A camera reading its own previous frame is animated by construction,
		// and one whose input redrew has to follow it
		if (NeedsHistory.count(camera)) {
			sceneMatches = false;
		}

		// So is one whose projection moves inside the pixel every frame, or
		// whose passes read a picture of last frame: both paint something
		// different from a scene that has not moved, which is exactly the case
		// the cache would otherwise answer out of what it kept. A temporal pass
		// converges by being run, so freezing it would freeze it half-resolved.
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
			// Moving again, so the settle count starts over and the cache that
			// was taken of the old picture is worthless
			camera->StillFrames = 0;
			camera->HasDrawn = true;
			plan.WriteCache = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		camera->StillFrames++;

		// Still settling: draw it properly and do not pay for a copy yet
		if (camera->StillFrames < CACHE_AFTER_STILL_FRAMES) {
			plan.WriteCache = false;
			RedrawnThisFrame.insert(camera);
			return plan;
		}

		if (!hasDynamicTail) {
			// Nothing animates and nothing moved, so the target already holds
			// the finished picture and there is nothing to copy or rerun
			plan.Skip = true;
			return plan;
		}

		// The frame it settles on is the one that takes the copy; from the next
		// frame on only the animated tail runs
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

		RedrawPlan plan = PlanRedraw(drawContext, *target);
		if (plan.Skip) {
			// Its target already holds the finished picture, so there is
			// nothing to record. Handing it back anyway is what lets a camera
			// drawing to the window present the same pixels again instead of
			// rendering the world a second time to arrive at them.
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
			// Put the cached half back where the chain expects to find it, then
			// pick up at the first pass that has to run again
			SDL_GPUBlitInfo restore{
				.source = {.texture = target->CacheTexture, .w = target->Width, .h = target->Height},
				.destination = {.texture = target->ColorTexture, .w = target->Width, .h = target->Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &restore);
		} else {
			// No cache to restore from, so fall back to drawing it properly
			if (!RecordCameraPasses(commands, drawContext, *target)) {
				return nullptr;
			}
			plan.FirstShader = 0;
			plan.WriteCache = true;
		}

		RecordShaderChain(commands, camera, *target, plan.FirstShader, plan.WriteCache);
		RecordHistoryCopy(commands, camera, *target);
		// Its target now holds a different picture, which is what a part showing
		// this camera needs to know. The plan.Skip path above returns before
		// here on purpose: nothing was rewritten, so nothing looking at it has
		// been given a reason to redraw.
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

		// A camera that cannot be recorded is skipped rather than sinking the
		// whole batch; the others have nothing to do with its failure
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

		// This runs from a script, which may well have moved something since
		// the engine last took the hash at the top of the frame. Everything
		// below compares against it -- what each camera can see, and whether
		// a dependency may keep its cached picture -- so it has to describe
		// the world being drawn now rather than the world one frame ago.
		SceneSignature = ComputeSceneSignature(drawContext.WorldRoot, drawContext.LightDirection);

		// Anything this camera samples has to be drawn first, or it would read
		// whatever was in that target from a previous frame. They go into the
		// same command buffer as the readback, ahead of it, so recording order
		// is what puts them first.
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

		// Ahead of this camera in the same command buffer, so their targets
		// hold this frame's picture by the time the shader chain samples them
		for (auto &dependency : dependencies) {
			RecordOffscreenCamera(commands, dependency);
		}

		if (!RecordCameraPasses(commands, drawContext, *target)) {
			SDL_CancelGPUCommandBuffer(commands);
			SDL_ReleaseGPUTransferBuffer(Gpu, transferBuffer);
			return false;
		}

		// The readback has to see what the shaders produced, not the raw render
		RecordShaderChain(commands, camera, *target, 0, false);
		RecordHistoryCopy(commands, camera, *target);
		// An explicit Render() rewrites the target like any other draw, so a
		// part showing this camera has to hear about it too
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
			// The window has to show something every frame, but that is a
			// blit, not a redraw. A still scene records nothing here and the
			// blit below sends last frame's picture again -- same pixels,
			// none of the work.
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
			SubmitTracked(commands);
			return;
		}

		// No offscreen target on this path, so there is nowhere to keep a
		// history or motion vectors -- but a SurfaceShader can still ask to
		// jitter, and a camera that was jittering and then had its whole chain
		// taken away has to be told to stop, or it keeps the last offset for
		// good.
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
		// Drawing straight to the swapchain skips PlanRedraw entirely, so this
		// path pays for its own walk
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

		SubmitTracked(frameContext.Commands);
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
