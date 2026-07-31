// Pictures on their way to the GPU: the one-pixel white fallback, the
// EditableImages parts show on their surfaces, and the slot table the opaque
// pass batches by. Uploaded once per revision and shared by every part
// pointing at the same image.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/render/RenderPass.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace gargantuan {
	void RenderProvider::EnsureWhiteTextureAndSamplers() {
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
		if (PartTexturesResolved && ResolvedSurfaceSignature == SceneDrawIndex.SurfaceSignature) {
			return;
		}

		PartTextures.clear();
		// Slot 0 is no picture, which the pass draws with its white texture.
		SurfaceTexturesBySlot.assign(1, nullptr);
		AllSurfacesGotSlots = true;
		// Every slot below is about to be reassigned, and keys hold slots
		SceneDrawIndex.DrawKeysStale = true;
		ResolvedSurfaceSignature = SceneDrawIndex.SurfaceSignature;
		PartTexturesResolved = true;

		if (!worldRoot) {
			return;
		}

		// Every part, not just the ones showing something: a part that lost its
		// picture has to lose its slot with it.
		for (BasePart *part : worldRoot->Parts.Raw()) {
			SDL_GPUTexture *texture = nullptr;

			if (part->GetSurfaceOrDefault().Camera) {
				auto it = CameraTargets.find(part->GetSurfaceOrDefault().Camera.get());
				if (it != CameraTargets.end() && it->second.ColorTexture) {
					texture = it->second.ColorTexture;
				}
			}
			if (!texture && part->GetSurfaceOrDefault().Image) {
				texture = GetOrUploadImageTexture(part->GetSurfaceOrDefault().Image.get());
			}

			if (!texture) {
				part->EnsureSurface().TextureSlot = 0;
				continue;
			}

			PartTextures[part] = texture;

			uint8_t slot = 0;
			for (size_t candidate = 1; candidate < SurfaceTexturesBySlot.size(); candidate++) {
				if (SurfaceTexturesBySlot[candidate] == texture) {
					slot = (uint8_t)candidate;
					break;
				}
			}
			if (slot == 0) {
				if (SurfaceTexturesBySlot.size() >= MAX_SURFACE_SLOTS) {
					AllSurfacesGotSlots = false;
				} else {
					slot = (uint8_t)SurfaceTexturesBySlot.size();
					SurfaceTexturesBySlot.push_back(texture);
				}
			}
			part->EnsureSurface().TextureSlot = slot;
		}
	}

	SDL_GPUTexture *RenderProvider::GetOrUploadImageTexture(EditableImage *image) {
		if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
			return nullptr;
		}

		uint32_t width = (uint32_t)image->GetWidth();
		uint32_t height = (uint32_t)image->GetHeight();

		UploadedImage &uploaded = UploadedImages[image];
		bool hasMatchingTexture = uploaded.Texture != nullptr && uploaded.Width == width && uploaded.Height == height;
		if (hasMatchingTexture && uploaded.UploadedSourceRevision == image->GetRevision()) {
			return uploaded.Texture;
		}

		if (!hasMatchingTexture) {
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

		uint32_t pixelByteCount = width * height * EditableImage::CHANNELS;
		SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = pixelByteCount,
		};
		SDL_GPUTransferBuffer *transferBuffer = SDL_CreateGPUTransferBuffer(Gpu, &transferInfo);
		if (!transferBuffer) {
			return nullptr;
		}

		if (void *mapped = SDL_MapGPUTransferBuffer(Gpu, transferBuffer, false)) {
			std::memcpy(mapped, image->Pixels.data(), pixelByteCount);
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

		uploaded.UploadedSourceRevision = image->GetRevision();
		return uploaded.Texture;
	}

	// The samplers live here because EnsureWhiteTextureAndSamplers is what creates them.
	void RenderProvider::ReleaseTextureUploads() {
		for (auto &[_, uploaded] : UploadedImages) {
			if (uploaded.Texture) SDL_ReleaseGPUTexture(Gpu, uploaded.Texture);
		}
		UploadedImages.clear();
		PartTextures.clear();
		SurfaceTexturesBySlot.clear();

		if (WhiteTexture) {
			SDL_ReleaseGPUTexture(Gpu, WhiteTexture);
			WhiteTexture = nullptr;
		}

		SDL_GPUSampler **samplers[] = {&ShaderSampler, &PartSurfaceSampler, &PointSampler};
		for (SDL_GPUSampler **sampler : samplers) {
			if (*sampler) {
				SDL_ReleaseGPUSampler(Gpu, *sampler);
				*sampler = nullptr;
			}
		}
	}
}
