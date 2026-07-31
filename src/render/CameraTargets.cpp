// Every texture a camera draws into or reads back from: the colour/depth pair,
// the scratch and cache copies the shader chain ping-pongs through, and the
// temporal ones a shader only gets if it asks for history or motion.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/render/RenderPass.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace gargantuan {
	// The one place that knows what a target holds. Anything added to
	// CameraTextureSet is freed by every caller the moment it is freed here.
	void RenderProvider::ReleaseTargetTextures(CameraTextureSet &target) {
		SDL_GPUTexture **textures[] = {
			&target.ColorTexture,
			&target.ChainPingPongTexture,
			&target.HistoryTexture,
			&target.VelocityTexture,
			&target.ViewDepthTexture,
			&target.ViewDepthHistoryTexture,
			&target.CachedChainPrefixTexture,
			&target.DepthStencilAttachmentTexture,
		};

		for (SDL_GPUTexture **texture : textures) {
			if (*texture) {
				SDL_ReleaseGPUTexture(Gpu, *texture);
				*texture = nullptr;
			}
		}
	}

	void RenderProvider::ReleaseCameraTarget(Camera *camera) {
		auto it = CameraTargets.find(camera);
		if (it == CameraTargets.end()) {
			return;
		}

		ReleaseTargetTextures(it->second);
		CamerasNeedingHistory.erase(it->first);
		SceneDrawIndex.ForgetCamera(it->first);
		CameraTargets.erase(it);
	}

	// Teardown for the targets this file hands out; the uploads have their own.
	void RenderProvider::ReleaseCameraResources() {
		for (auto &[_, target] : CameraTargets) {
			ReleaseTargetTextures(target);
		}
		CameraTargets.clear();

	}

	// Camera targets serve every shader-chain input and output role.
	static constexpr SDL_GPUTextureUsageFlags CAMERA_TARGET_USAGE =
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER |
		SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

	RenderProvider::CameraTextureSet *RenderProvider::AcquireCameraTarget(Camera *camera, bool needsPingPongTexture) {
		if (!camera) {
			return nullptr;
		}

		auto size = camera->ViewportSize;
		uint32_t width = (uint32_t)glm::max(size.GetX(), 0.0f);
		uint32_t height = (uint32_t)glm::max(size.GetY(), 0.0f);
		if (width == 0 || height == 0) {
			return nullptr;
		}

		CameraTextureSet &target = CameraTargets[camera];
		bool isSizeCurrent = target.ColorTexture != nullptr && target.Width == width && target.Height == height;
		bool scratchReady = !needsPingPongTexture || target.ChainPingPongTexture != nullptr;
		if (isSizeCurrent && scratchReady) {
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

		if (isSizeCurrent) {
			CameraTextureGeneration++;
			target.ChainPingPongTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
			if (!target.ChainPingPongTexture) {
				SDL_Log("Failed to create a %ux%u shader scratch target: %s", width, height, SDL_GetError());
			}
			return &target;
		}

		ReleaseTargetTextures(target);

		CameraTextureGeneration++;
		target.ColorTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
		if (needsPingPongTexture) {
			target.ChainPingPongTexture = SDL_CreateGPUTexture(Gpu, &colorInfo);
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
		target.DepthStencilAttachmentTexture = SDL_CreateGPUTexture(Gpu, &depthInfo);

		target.Width = width;
		target.Height = height;

		if (!target.ColorTexture || !target.DepthStencilAttachmentTexture) {
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
			return GetOrUploadImageTexture(source.Image.get());
		}

		if (source.Camera) {
			auto it = CameraTargets.find(source.Camera.get());
			if (it == CameraTargets.end()) {
				return nullptr;
			}

			// Cycle-closing edges read the finished prior frame.
			if (PriorFrameReaderToSourceEdges.count({reader, source.Camera.get()}) && it->second.HistoryTexture) {
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

	SDL_GPUSampler *RenderProvider::GetTextureSourceSampler(const ShaderProperties::TextureSource &source) {
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
					needs.NeedsVelocityAndViewDepth = true;
					break;
				case Enums::RenderTexture::Velocity:
				case Enums::RenderTexture::Depth:
					needs.NeedsVelocityAndViewDepth = true;
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
		SDL_GPUCommandBuffer *commands, Camera *camera, CameraTextureSet &target, const TemporalNeeds &needs
	) {
		if (!camera || target.Width == 0 || target.Height == 0) {
			return;
		}

		if (needs.History) {
			// Allocate here; RecordTemporalHistoryCopies keeps it current.
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
		auto createMeasurementTexture = [&](SDL_GPUTexture *&texture, SDL_GPUTextureFormat format, const char *targetLabel) {
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
				SDL_Log("Failed to create a %ux%u %s target: %s", target.Width, target.Height, targetLabel, SDL_GetError());
			}
		};

		// Allocate both attachments because one pass declares and writes both.
		if (needs.NeedsVelocityAndViewDepth) {
			createMeasurementTexture(target.VelocityTexture, VELOCITY_FORMAT, "motion vector");
			createMeasurementTexture(target.ViewDepthTexture, VIEW_DEPTH_FORMAT, "view depth");
		}

		if (needs.DepthHistory && !target.ViewDepthHistoryTexture) {
			createMeasurementTexture(target.ViewDepthHistoryTexture, VIEW_DEPTH_FORMAT, "previous view depth");

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

		if (!IsVelocityPassUsedThisFrame) {
			// Drop stale transforms when velocity demand stops.
			if (!TransformsStamped) {
				return;
			}

			Scene.WorldRoot->ClearPreviousModelMatrices();
			TransformsStamped = false;
			return;
		}

		// Into the world's motion column, not onto each part.
		for (BasePart *part : Scene.WorldRoot->Parts.Raw()) {
			Scene.WorldRoot->StampPreviousModelMatrix(part->WorldIndex, part->GetModelMatrix());
		}
		TransformsStamped = true;
	}

	void RenderProvider::RecordTemporalHistoryCopies(
		SDL_GPUCommandBuffer *commands, Camera *camera, const CameraTextureSet &target
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
		if (!CamerasNeedingHistory.count(camera) && !needs.History) {
			return;
		}

		CameraTextureSet &mutableTarget = CameraTargets[camera];
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

	void RenderProvider::EnsureCacheTexture(CameraTextureSet &target) {
		if (target.CachedChainPrefixTexture || target.Width == 0 || target.Height == 0) {
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
		target.CachedChainPrefixTexture = SDL_CreateGPUTexture(Gpu, &info);
	}
}
