#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/InstanceData.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <magic_enum/magic_enum.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace gargantuan {
	static const glm::mat4 SHADOW_CLIP_TO_UV_MATRIX{
		0.5f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		-0.5f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.5f,
		0.5f,
		0.0f,
		1.0f
	};

	namespace {
		const PartSpan EMPTY_PARTS;
	} // namespace

	class OpaquePass final : public RenderPass {
	  public:
		struct alignas(16) WorldUniforms {
			glm::mat4 ViewMatrix;
			glm::mat4 ProjectionMatrix;
			glm::mat4 ShadowClipToUvMatrix;
			glm::vec4 LightDirection;
		};

		struct alignas(16) PartUniforms {
			glm::mat4 ModelMatrix;
			glm::vec4 Color;
		};

		struct alignas(16) PartFragmentUniforms {
			glm::vec4 HasSurfaceTexture;
			// xyz is the textured face's world-space normal.
			glm::vec4 SurfaceNormalAndRule;
			// xy tiles; zw offsets.
			glm::vec4 SurfaceTilingOffset;
		};

		// Matches the std430 block the instanced vertex stage reads
		FileShader InstancedShader{
			.VertexFilepathStem = GetShaderPath("opaque_instanced.vert"),
			.VertexUniformBufferCount = 1,
			// Two: the instances, and the row indices that select them.
			// opaque_instanced.vert reads both, and binding only the first is what
			// left every instance reading whatever happened to be at index zero.
			.VertexStorageBufferCount = 2,
			.FragmentFilepathStem = GetShaderPath("opaque_instanced.frag"),
			.FragmentUniformBufferCount = 2,
			.FragmentSamplerCount = 2,
		};
		SDL_GPUGraphicsPipeline *InstancedPipeline = nullptr;

		// The scene's instances, one per grid slot, kept across frames. A slot is
		// stable while its part stays in its cell, so the frame only writes the
		// slots that changed instead of the whole visible set.
		SDL_GPUBuffer *InstanceBuffer = nullptr;
		SDL_GPUTransferBuffer *InstanceTransfer = nullptr;
		uint32_t InstanceCapacity = 0;
		// True until the buffer has been filled once. Deltas mean nothing against a
		// buffer that has never held anything.
		bool InstancesSeeded = false;
		// Retained to avoid reallocating large instance sets.
		std::vector<PartInstance> InstanceScratch;

		// This camera's visible set as slot indices in batch order -- four bytes an
		// entry against the ninety-six a scattered instance costs. Rebuilt every
		// frame because it is a property of the view, not of the scene.
		SDL_GPUBuffer *InstanceIndexBuffer = nullptr;
		SDL_GPUTransferBuffer *InstanceIndexTransfer = nullptr;
		uint32_t InstanceIndexCapacity = 0;
		std::vector<uint32_t> InstanceIndexScratch;
		struct Batch {
			GpuMesh *Mesh = nullptr;
			// Texture is bound once per mesh-texture batch.
			SDL_GPUTexture *Texture = nullptr;
			Enums::PartType Shape = Enums::PartType::Block;
			uint32_t FirstInstance = 0;
			uint32_t Count = 0;
		};
		std::vector<Batch> Batches;
		// Visible-list index to batch index.
		static constexpr uint32_t SKIPPED = 0xFFFFFFFFu;
		std::vector<uint32_t> BatchIndexPerPart;
		std::vector<uint32_t> Cursors;
		// (MeshId, texture slot) to batch. Both travel on the part as small
		// numbers, so the pair indexes this instead of being searched for.
		// Only the keys actually used are reset, so the table is never walked.
		std::array<uint32_t, MAX_MESH_IDS * MAX_SURFACE_SLOTS> KeyToBatch;
		std::vector<uint16_t> UsedKeys;
		// Timed per phase to avoid per-part probe cost.
		uint64_t InstanceBucketNanoseconds = 0;
		uint64_t InstanceFillNanoseconds = 0;
		uint64_t InstanceUploadNanoseconds = 0;

		void Destroy(SDL_GPUDevice *gpu) override {
			if (InstancedPipeline) {
				SDL_ReleaseGPUGraphicsPipeline(gpu, InstancedPipeline);
				InstancedPipeline = nullptr;
			}
			if (InstanceBuffer) {
				SDL_ReleaseGPUBuffer(gpu, InstanceBuffer);
				InstanceBuffer = nullptr;
			}
			if (InstanceIndexBuffer) {
				SDL_ReleaseGPUBuffer(gpu, InstanceIndexBuffer);
				InstanceIndexBuffer = nullptr;
			}
			if (InstanceIndexTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceIndexTransfer);
				InstanceIndexTransfer = nullptr;
			}
			InstanceIndexCapacity = 0;
			InstancesSeeded = false;
			if (InstanceTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceTransfer);
				InstanceTransfer = nullptr;
			}
			InstancedShader.Destroy(gpu);
			RenderPass::Destroy(gpu);
		}

		// Double capacity so steady scenes stop reallocating.
		bool EnsureInstanceCapacity(SDL_GPUDevice *gpu, uint32_t count) {
			if (count <= InstanceCapacity && InstanceBuffer && InstanceTransfer) {
				return true;
			}

			uint32_t capacity = InstanceCapacity ? InstanceCapacity : 1024;
			while (capacity < count) {
				capacity *= 2;
			}

			if (InstanceBuffer) {
				SDL_ReleaseGPUBuffer(gpu, InstanceBuffer);
			}
			if (InstanceTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceTransfer);
			}

			SDL_GPUBufferCreateInfo bufferInfo{
				.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
				.size = capacity * (uint32_t)sizeof(PartInstance),
			};
			InstanceBuffer = SDL_CreateGPUBuffer(gpu, &bufferInfo);

			SDL_GPUTransferBufferCreateInfo transferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = capacity * (uint32_t)sizeof(PartInstance),
			};
			InstanceTransfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);

			InstanceCapacity = InstanceBuffer && InstanceTransfer ? capacity : 0;
			// A new buffer holds nothing, so the next upload cannot be a delta
			// however few slots the frame says changed.
			InstancesSeeded = false;
			return InstanceCapacity > 0;
		}

		bool EnsureIndexCapacity(SDL_GPUDevice *gpu, uint32_t count) {
			if (count <= InstanceIndexCapacity && InstanceIndexBuffer && InstanceIndexTransfer) {
				return true;
			}

			uint32_t capacity = InstanceIndexCapacity ? InstanceIndexCapacity : 1024;
			while (capacity < count) {
				capacity *= 2;
			}

			if (InstanceIndexBuffer) {
				SDL_ReleaseGPUBuffer(gpu, InstanceIndexBuffer);
			}
			if (InstanceIndexTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceIndexTransfer);
			}

			SDL_GPUBufferCreateInfo bufferInfo{
				.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
				.size = capacity * (uint32_t)sizeof(uint32_t),
			};
			InstanceIndexBuffer = SDL_CreateGPUBuffer(gpu, &bufferInfo);

			SDL_GPUTransferBufferCreateInfo transferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = capacity * (uint32_t)sizeof(uint32_t),
			};
			InstanceIndexTransfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);

			InstanceIndexCapacity = InstanceIndexBuffer && InstanceIndexTransfer ? capacity : 0;
			return InstanceIndexCapacity > 0;
		}

		// Only for a draw with no row table behind it: same arithmetic, built
		// on the spot. See SyncPartRowsFromWorld for the comments.
		static void BuildInstanceFallback(BasePart *part, PartInstance &instance) {
			const float *rotation = reinterpret_cast<const float *>(&part->Transform.CFrame.Rotation);
			const glm::vec3 &size = part->Transform.Size;
			const glm::vec3 &position = part->Transform.CFrame.Position;
			float *model = reinterpret_cast<float *>(instance.ModelRows);

			model[0] = rotation[0] * size.x;
			model[1] = rotation[3] * size.y;
			model[2] = rotation[6] * size.z;
			model[3] = position.x;
			model[4] = rotation[1] * size.x;
			model[5] = rotation[4] * size.y;
			model[6] = rotation[7] * size.z;
			model[7] = position.y;
			model[8] = rotation[2] * size.x;
			model[9] = rotation[5] * size.y;
			model[10] = rotation[8] * size.z;
			model[11] = position.z;

			const Color3 &colour = part->Visual.Color;
			instance.Color.x = colour.R;
			instance.Color.y = colour.G;
			instance.Color.z = colour.B;
			instance.Color.w = 1.0f - part->Visual.Transparency;

			// One sparse-set lookup for the match and the four transforms below.
			const components::Surface &surface = part->GetSurfaceOrDefault();
			glm::vec4 match = BasePart::SurfaceMatchOf(surface.Face);
			float nx = match.x, ny = match.y, nz = match.z;
			if (nx * nx + ny * ny + nz * nz > 0.0f) {
				float wx = rotation[0] * nx + rotation[3] * ny + rotation[6] * nz;
				float wy = rotation[1] * nx + rotation[4] * ny + rotation[7] * nz;
				float wz = rotation[2] * nx + rotation[5] * ny + rotation[8] * nz;
				float length = std::sqrt(wx * wx + wy * wy + wz * wz);
				if (length > 0.0f) {
					nx = wx / length;
					ny = wy / length;
					nz = wz / length;
				}
			}

			instance.SurfaceNormalAndRule.x = nx;
			instance.SurfaceNormalAndRule.y = ny;
			instance.SurfaceNormalAndRule.z = nz;
			instance.SurfaceNormalAndRule.w = match.w;
			instance.SurfaceTilingOffset.x = surface.Tiling.Value.x;
			instance.SurfaceTilingOffset.y = surface.Tiling.Value.y;
			instance.SurfaceTilingOffset.z = surface.Offset.Value.x;
			instance.SurfaceTilingOffset.w = surface.Offset.Value.y;
		}

		// Returns false for no work or allocation failure.
		bool PrepareInstances(
			SDL_GPUDevice *gpu,
			FrameContext &context,
			PartSpan parts,
			bool anyPartTextures
		) {
			InstanceBucketNanoseconds = 0;
			InstanceFillNanoseconds = 0;
			InstanceUploadNanoseconds = 0;
			uint64_t started = SDL_GetTicksNS();

			Batches.clear();
			for (uint16_t key : UsedKeys) {
				KeyToBatch[key] = SKIPPED;
			}
			UsedKeys.clear();
			Batches.reserve(64);

			// Every element is overwritten; clearing adds a measured 9 MB memset.
			if (InstanceScratch.size() < parts.size()) {
				InstanceScratch.resize(parts.size());
			}
			if (BatchIndexPerPart.size() < parts.size()) {
				BatchIndexPerPart.resize(parts.size());
			}

			const bool useSlots = context.SurfaceTextures && context.SurfaceSlotsComplete;
			SDL_GPUTexture *const *surfaceTextures = useSlots ? context.SurfaceTextures->data() : nullptr;
			const uint32_t surfaceTextureCount = useSlots ? (uint32_t)context.SurfaceTextures->size() : 0;

			BasePart *const *partList = parts.Parts;
			uint32_t *partBatchOut = BatchIndexPerPart.data();
			Batch *batches = Batches.data();
			const size_t count = parts.size();

			const PartInstance *instancesBySlot = nullptr;
			const uint32_t *drawIndices = nullptr;
			const uint16_t *keys = nullptr;
			if (context.InstancesBySlot && context.VisibleParts &&
				context.VisibleParts->InViewIndexList.size() >= count &&
				context.InstancesBySlot->size() >= context.WorldRoot->Parts.Raw().size()) {
				instancesBySlot = context.InstancesBySlot->data();
				drawIndices = context.VisibleParts->InViewIndexList.data();
				if (context.MeshTextureBatchKeys && context.MeshTextureBatchKeys->size() == context.InstancesBySlot->size()) {
					keys = context.MeshTextureBatchKeys->data();
				}
			}

			auto openBatch = [&](BasePart *part, SDL_GPUTexture *texture) -> uint32_t {
				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					return SKIPPED;
				}

				auto *primitive = part->Cast<Part>();
				uint32_t opened = (uint32_t)Batches.size();
				Batches.push_back(
					{mesh.get(), texture, primitive ? primitive->GetShape() : Enums::PartType::Block, 0, 0}
				);
				return opened;
			};

			for (size_t index = 0; index < count; index++) {
				// Zero is no key, and sends this part round the long way
				uint32_t key = 0;
				if (useSlots) {
					if (keys) {
						key = keys[drawIndices[index]];
					} else {
						BasePart *part = partList[index];
						uint32_t meshId = part->Visual.MeshId;
						if (meshId != 0 && meshId < MAX_MESH_IDS) {
							key = meshId * MAX_SURFACE_SLOTS + part->GetSurfaceOrDefault().TextureSlot;
						}
					}
				}

				uint32_t bucket = SKIPPED;
				if (key != 0) {
					bucket = KeyToBatch[key];
					if (bucket == SKIPPED) {
						BasePart *part = partList[index];
						uint32_t slot = key % MAX_SURFACE_SLOTS;
						SDL_GPUTexture *texture = slot != 0 && slot < surfaceTextureCount
							? surfaceTextures[slot]
							: context.WhiteTexture;
						bucket = openBatch(part, texture ? texture : context.WhiteTexture);
						batches = Batches.data();
						if (bucket != SKIPPED) {
							KeyToBatch[key] = bucket;
							UsedKeys.push_back((uint16_t)key);
						}
					}
				} else {
					BasePart *part = partList[index];
					SDL_GPUTexture *texture = context.WhiteTexture;
					const components::Surface &surface = part->GetSurfaceOrDefault();
					if (anyPartTextures && (surface.Camera || surface.Image)) {
						auto found = context.PartTextures->find(part);
						if (found != context.PartTextures->end() && found->second) {
							texture = found->second;
						}
					}

					auto &mesh = part->GetMesh();
					if (mesh && mesh->VertexBuffer && mesh->IndexBuffer) {
						for (uint32_t candidate = 0; candidate < (uint32_t)Batches.size(); candidate++) {
							if (batches[candidate].Mesh == mesh.get() && batches[candidate].Texture == texture) {
								bucket = candidate;
								break;
							}
						}
						if (bucket == SKIPPED) {
							bucket = openBatch(part, texture);
							batches = Batches.data();
						}
					}
				}

				partBatchOut[index] = bucket;
				if (bucket != SKIPPED) {
					batches[bucket].Count++;
				}
			}

			InstanceBucketNanoseconds = SDL_GetTicksNS() - started;
			if (Batches.empty()) {
				return false;
			}

			uint64_t fillStarted = SDL_GetTicksNS();

			// Prefix offsets let the fill pass write directly.
			uint32_t totalInstanceCount = 0;
			for (Batch &batch : Batches) {
				batch.FirstInstance = totalInstanceCount;
				totalInstanceCount += batch.Count;
			}

			// Retained to avoid repeat allocation.
			Cursors.resize(Batches.size());
			for (size_t index = 0; index < Batches.size(); index++) {
				Cursors[index] = Batches[index].FirstInstance;
			}

			SDL_GPUTexture *White = context.WhiteTexture;

			// Cache vector storage used repeatedly per part.
			uint32_t *cursors = Cursors.data();

			if (InstanceIndexScratch.size() < totalInstanceCount) {
				InstanceIndexScratch.resize(totalInstanceCount);
			}
			uint32_t *indices = InstanceIndexScratch.data();

			if (instancesBySlot) {
				// Slot indices, not instances. The instance behind a slot is already
				// on the GPU and already correct for every part that did not move,
				// so all the scatter has to place is which slot each draw wants.
				for (size_t index = 0; index < count; index++) {
					uint32_t bucket = partBatchOut[index];
					if (bucket == SKIPPED) {
						continue;
					}
					indices[cursors[bucket]++] = drawIndices[index];
				}
			} else {
				// No row table, so build each one here the way it used to be. There
				// are no slots to point at either, so the instances go up
				// contiguously and the indices are the identity.
				PartInstance *scratch = InstanceScratch.data();
				for (size_t index = 0; index < count; index++) {
					uint32_t bucket = partBatchOut[index];
					if (bucket == SKIPPED) {
						continue;
					}
					uint32_t at = cursors[bucket]++;
					BuildInstanceFallback(partList[index], scratch[at]);
					indices[at] = at;
				}
			}

			InstanceFillNanoseconds = SDL_GetTicksNS() - fillStarted;
			uint64_t uploadStarted = SDL_GetTicksNS();

			// The scene buffer is indexed by slot, so it has to be as big as the slot
			// table and not as big as what is visible.
			const uint32_t sceneCount = instancesBySlot ? (uint32_t)context.InstancesBySlot->size() : totalInstanceCount;
			if (!EnsureInstanceCapacity(gpu, sceneCount) || !EnsureIndexCapacity(gpu, totalInstanceCount)) {
				InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
				return false;
			}

			if (!UploadInstances(gpu, context, instancesBySlot, sceneCount)) {
				InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
				return false;
			}

			void *mappedIndices = SDL_MapGPUTransferBuffer(gpu, InstanceIndexTransfer, true);
			if (!mappedIndices) {
				InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
				return false;
			}
			std::memcpy(mappedIndices, InstanceIndexScratch.data(), totalInstanceCount * sizeof(uint32_t));
			SDL_UnmapGPUTransferBuffer(gpu, InstanceIndexTransfer);

			SDL_GPUCopyPass *indexPass = SDL_BeginGPUCopyPass(context.Commands);
			SDL_GPUTransferBufferLocation indexSource{.transfer_buffer = InstanceIndexTransfer, .offset = 0};
			SDL_GPUBufferRegion indexDestination{
				.buffer = InstanceIndexBuffer, .offset = 0, .size = totalInstanceCount * (uint32_t)sizeof(uint32_t)
			};
			// Cycle transfer storage to avoid waiting on the prior frame. Safe for
			// this one because it is rewritten whole every frame -- unlike the scene
			// buffer, which must keep what the last frame left in it.
			SDL_UploadToGPUBuffer(indexPass, &indexSource, &indexDestination, true);
			SDL_EndGPUCopyPass(indexPass);

			ProfilerCount("v1.instances.indexBytes", (uint64_t)totalInstanceCount * sizeof(uint32_t));
			ProfilerCount("v1.instances.bytesIfScattered", (uint64_t)totalInstanceCount * sizeof(PartInstance));

			InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
			return true;
		}

		// The scene's instances, sent whole the first time and by changed slot after
		// that.
		//
		// The buffer must NOT be cycled: cycling hands back a fresh allocation, and a
		// delta written into a buffer that does not hold the previous frame leaves
		// every unwritten slot as rubbish. The transfer buffer is not cycled either,
		// for the same reason the copy is one range -- the writes are scattered and
		// a copy per changed slot would be thousands of tiny copies.
		bool UploadInstances(
			SDL_GPUDevice *gpu,
			FrameContext &context,
			const PartInstance *instancesBySlot,
			uint32_t sceneCount
		) {
			if (!instancesBySlot) {
				// Fallback path: the scratch is already contiguous and complete.
				void *mapped = SDL_MapGPUTransferBuffer(gpu, InstanceTransfer, true);
				if (!mapped) {
					return false;
				}
				std::memcpy(mapped, InstanceScratch.data(), sceneCount * sizeof(PartInstance));
				SDL_UnmapGPUTransferBuffer(gpu, InstanceTransfer);

				SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(context.Commands);
				SDL_GPUTransferBufferLocation source{.transfer_buffer = InstanceTransfer, .offset = 0};
				SDL_GPUBufferRegion destination{
					.buffer = InstanceBuffer, .offset = 0, .size = sceneCount * (uint32_t)sizeof(PartInstance)
				};
				SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
				SDL_EndGPUCopyPass(copyPass);
				InstancesSeeded = false;
				return true;
			}

			const std::vector<uint32_t> *dirty = context.DirtyInstanceSlots;
			const bool uploadWholeBuffer = context.InstancesAllDirty || !InstancesSeeded || !dirty;

			// Nothing moved and the buffer already holds the scene. This is the
			// still-frame case and it is the whole point of the exercise.
			if (!uploadWholeBuffer && dirty->empty()) {
				ProfilerCount("v1.instances.slotsWritten", 0);
				ProfilerCount("v1.instances.bytesCopied", 0);
				return true;
			}

			auto *mapped = static_cast<PartInstance *>(SDL_MapGPUTransferBuffer(gpu, InstanceTransfer, false));
			if (!mapped) {
				return false;
			}

			uint32_t lowest = 0, highest = sceneCount;
			if (uploadWholeBuffer) {
				std::memcpy(mapped, instancesBySlot, sceneCount * sizeof(PartInstance));
			} else {
				// Sparse writes into the mapped buffer, then one range copy across
				// the span they touched. Coalescing into a copy per run was measured
				// worse on a scattered dirty set: the runs are short and thousands of
				// small copies cost more than one large one.
				lowest = UINT32_MAX;
				highest = 0;
				for (uint32_t slot : *dirty) {
					if (slot >= sceneCount) {
						continue;
					}
					mapped[slot] = instancesBySlot[slot];
					lowest = std::min(lowest, slot);
					highest = std::max(highest, slot + 1);
				}
				if (lowest >= highest) {
					SDL_UnmapGPUTransferBuffer(gpu, InstanceTransfer);
					return true;
				}
			}
			SDL_UnmapGPUTransferBuffer(gpu, InstanceTransfer);

			const uint32_t offset = uploadWholeBuffer ? 0 : lowest;
			const uint32_t span = uploadWholeBuffer ? sceneCount : highest - lowest;

			SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(context.Commands);
			SDL_GPUTransferBufferLocation source{
				.transfer_buffer = InstanceTransfer, .offset = offset * (uint32_t)sizeof(PartInstance)
			};
			SDL_GPUBufferRegion destination{
				.buffer = InstanceBuffer,
				.offset = offset * (uint32_t)sizeof(PartInstance),
				.size = span * (uint32_t)sizeof(PartInstance),
			};
			SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
			SDL_EndGPUCopyPass(copyPass);

			InstancesSeeded = true;
			ProfilerCount("v1.instances.slotsWritten", uploadWholeBuffer ? sceneCount : dirty->size());
			ProfilerCount("v1.instances.bytesCopied", (uint64_t)span * sizeof(PartInstance));
			return true;
		}

		OpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
			KeyToBatch.fill(SKIPPED);
			InstancedShader.Init(gpu);
			InstancedPipeline = PipelineBuilder()
									.SetVertexShader(InstancedShader.VertexShader)
									.SetFragmentShader(InstancedShader.FragmentShader)
									.SetColorEnabled(true)
									.SetColorFormat(swapchainFormat)
									.SetBlendingEnabled(true)
									.SetDepthEnabled(true)
									.SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
									.Build(gpu);

			PassShader = FileShader{
				.VertexFilepathStem = GetShaderPath("opaque.vert"),
				.VertexUniformBufferCount = 2,
				.FragmentFilepathStem = GetShaderPath("opaque.frag"),
				.FragmentUniformBufferCount = 2,
				.FragmentSamplerCount = 2,
			};
			PassShader.Init(gpu);
			Pipeline = PipelineBuilder()
						   .SetVertexShader(PassShader.VertexShader)
						   .SetFragmentShader(PassShader.FragmentShader)
						   .SetColorEnabled(true)
						   .SetColorFormat(swapchainFormat)
						   .SetBlendingEnabled(true)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			// Asked once, outside the measured per-part path.
			const bool measuring = G_PROFILE_COLLECTING();
			uint64_t transformNanoseconds = 0;
			uint64_t submitNanoseconds = 0;
			uint64_t partsDrawn = 0;
			uint64_t individualParts = 0;

			// Publish once; per-part updates scanned counter names twice.
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeDraws{};
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeTriangles{};

			// Instance upload must finish before opening the render pass.
			std::vector<BasePart *> allSceneParts;
			PartSpan drawList;
			if (context.VisibleParts) {
				drawList = context.VisibleParts->InViewParts();
			} else {
				auto raw = context.WorldRoot->Parts.Raw();
				allSceneParts.assign(raw.begin(), raw.end());
				drawList = {allSceneParts.data(), allSceneParts.size()};
			}

			const bool anyPartTextures = context.PartTextures && !context.PartTextures->empty();

			// One draw per mesh-texture pair; SurfaceShader stays per-part.
			bool instanced = InstancedPipeline && !context.SurfacePipeline && drawList.size() > 0 &&
				PrepareInstances(gpu, context, drawList, anyPartTextures);

			SDL_GPUColorTargetInfo colorTarget = {
				.texture = context.ColorTargetTexture,
				.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
			};

			SDL_GPUDepthStencilTargetInfo depthTarget = {
				.texture = context.DepthTexture,
				.clear_depth = 1.0f,
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_DONT_CARE,
				.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
				.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
			};

			SDL_GPUTextureSamplerBinding shadowBinding{
				.texture = context.ShadowMapTexture,
				.sampler = context.ShadowSampler,
			};

			auto pass = SDL_BeginGPURenderPass(context.Commands, &colorTarget, 1, &depthTarget);
			// SurfaceShader replaces the fragment stage.
			SDL_BindGPUGraphicsPipeline(
				pass,
				instanced ? InstancedPipeline : (context.SurfacePipeline ? context.SurfacePipeline : Pipeline)
			);

			// Surface images follow the shadow-map binding.
			if (context.SurfacePipeline && context.SurfaceSamplers && context.SurfaceSamplerCount > 0) {
				SDL_BindGPUFragmentSamplers(pass, 0, context.SurfaceSamplers, context.SurfaceSamplerCount);
			}

			WorldUniforms worldUniforms{
				.ViewMatrix = context.Camera->GetViewMatrix(),
				// Jitter only when a temporal pass requests varied pixel coverage.
				.ProjectionMatrix = context.Camera->GetJitteredProjectionMatrix(),
				.ShadowClipToUvMatrix = SHADOW_CLIP_TO_UV_MATRIX * context.LightViewProjectionMatrix,
				.LightDirection = glm::vec4(context.LightDirection, 0.0f),
			};
			SDL_PushGPUVertexUniformData(context.Commands, 0, &worldUniforms, sizeof(WorldUniforms));
			SDL_PushGPUFragmentUniformData(context.Commands, 0, &worldUniforms, sizeof(WorldUniforms));

			if (context.SurfacePipeline && context.SurfaceParameters && context.SurfaceParameterBytes > 0) {
				SDL_PushGPUFragmentUniformData(
					context.Commands, 1, context.SurfaceParameters, context.SurfaceParameterBytes
				);
			}

			// Bind state does not carry across render passes.
			SDL_GPUTexture *boundSurfaceTexture = nullptr;
			SDL_GPUBuffer *boundVertexBuffer = nullptr;
			SDL_GPUBuffer *boundIndexBuffer = nullptr;
			bool pushedBareFragment = false;

			if (instanced) {
				// Both, in the order opaque_instanced.vert declares them: the
				// scene's instances, then the row indices that select them.
				SDL_GPUBuffer *storage[2] = {InstanceBuffer, InstanceIndexBuffer};
				SDL_BindGPUVertexStorageBuffers(pass, 0, storage, 2);

				uint64_t submitStart = measuring ? SDL_GetTicksNS() : 0;
				for (const Batch &batch : Batches) {
					// All instances in a batch share texture presence.
					PartFragmentUniforms batchFragmentUniforms{
						.HasSurfaceTexture =
							glm::vec4(batch.Texture != context.WhiteTexture ? 1.0f : 0.0f, 0, 0, 0),
						.SurfaceNormalAndRule = glm::vec4(0.0f),
						.SurfaceTilingOffset = glm::vec4(0.0f),
					};
					SDL_PushGPUFragmentUniformData(
						context.Commands, 1, &batchFragmentUniforms, sizeof(PartFragmentUniforms)
					);

					SDL_GPUTextureSamplerBinding partBindings[2] = {
						shadowBinding,
						{.texture = batch.Texture, .sampler = context.SurfaceTextureSampler},
					};
					SDL_BindGPUFragmentSamplers(pass, 0, partBindings, 2);

					SDL_GPUBufferBinding vertexBinding{.buffer = batch.Mesh->VertexBuffer, .offset = 0};
					SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

					SDL_GPUBufferBinding indexBinding{.buffer = batch.Mesh->IndexBuffer, .offset = 0};
					SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

					// gl_InstanceIndex uses First as its direct buffer offset.
					SDL_DrawGPUIndexedPrimitives(
						pass, batch.Mesh->IndexCount, batch.Count, 0, 0, batch.FirstInstance
					);
					partsDrawn += batch.Count;
				}

				if (measuring) {
					submitNanoseconds += SDL_GetTicksNS() - submitStart;
					ProfilerCountTime("Bucket ms", InstanceBucketNanoseconds);
					ProfilerCountTime("Fill Instances ms", InstanceFillNanoseconds);
					ProfilerCountTime("Upload ms", InstanceUploadNanoseconds);
					ProfilerCountTime("Batch Submit ms", submitNanoseconds);
					ProfilerCount("Batches", (uint64_t)Batches.size());
					submitNanoseconds = 0;
					for (const Batch &batch : Batches) {
						auto index = magic_enum::enum_index(batch.Shape);
						if (index && *index < shapeDraws.size()) {
							shapeDraws[*index] += batch.Count;
							shapeTriangles[*index] += (uint64_t)batch.Count * (batch.Mesh->IndexCount / 3);
						}
					}
				}
			}

			// Continue from bindings left by the instanced path.
			if (instanced) {
				boundSurfaceTexture = context.WhiteTexture;
				pushedBareFragment = true;
			}

			for (BasePart *part : instanced ? EMPTY_PARTS : drawList) {
				uint64_t partStart = measuring ? SDL_GetTicksNS() : 0;

				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				PartUniforms uniforms{
					.ModelMatrix = part->GetModelMatrix(),
					.Color = glm::vec4((glm::vec3)part->Visual.Color, 1.0f - part->Visual.Transparency),
				};
				// Keep CPU preparation outside the measured driver-call section.
				uint64_t partSubmit = measuring ? SDL_GetTicksNS() : 0;
				SDL_PushGPUVertexUniformData(context.Commands, 1, &uniforms, sizeof(PartUniforms));

				// Only the engine fragment stage consumes part textures.
				if (!context.SurfacePipeline) {
					SDL_GPUTexture *surfaceTexture = context.WhiteTexture;
					if (anyPartTextures) {
						auto it = context.PartTextures->find(part);
						if (it != context.PartTextures->end() && it->second) {
							surfaceTexture = it->second;
						}
					}

					bool textured = surfaceTexture != context.WhiteTexture;

					// Bare parts share this block; later fields are gated off.
					if (textured || !pushedBareFragment) {
						// Avoid unused surface transforms.
						glm::vec3 surfaceNormal(0.0f);
						glm::vec4 surfaceMatch(0.0f);
						const components::Surface &partSurface = part->GetSurfaceOrDefault();
						if (textured) {
							surfaceMatch = BasePart::SurfaceMatchOf(partSurface.Face);
							surfaceNormal = glm::vec3(surfaceMatch);
							if (glm::dot(surfaceNormal, surfaceNormal) > 0.0f) {
								surfaceNormal =
									glm::normalize(glm::mat3(uniforms.ModelMatrix) * surfaceNormal);
							}
						}

						PartFragmentUniforms fragmentUniforms{
							.HasSurfaceTexture = glm::vec4(textured ? 1.0f : 0.0f, 0, 0, 0),
							.SurfaceNormalAndRule = glm::vec4(surfaceNormal, surfaceMatch.w),
							.SurfaceTilingOffset = glm::vec4(
								partSurface.Tiling.GetX(),
								partSurface.Tiling.GetY(),
								partSurface.Offset.GetX(),
								partSurface.Offset.GetY()
							),
						};
						SDL_PushGPUFragmentUniformData(
							context.Commands, 1, &fragmentUniforms, sizeof(PartFragmentUniforms)
						);
						pushedBareFragment = !textured;
					}

					// Rebind only on change; most parts share the white texture.
					if (surfaceTexture != boundSurfaceTexture) {
						SDL_GPUTextureSamplerBinding partBindings[2] = {
							shadowBinding,
							{.texture = surfaceTexture, .sampler = context.SurfaceTextureSampler},
						};
						SDL_BindGPUFragmentSamplers(pass, 0, partBindings, 2);
						boundSurfaceTexture = surfaceTexture;
					}
				}

				// Primitive meshes bind once per shape.
				if (mesh->VertexBuffer != boundVertexBuffer) {
					SDL_GPUBufferBinding vertexBinding{.buffer = mesh->VertexBuffer, .offset = 0};
					SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
					boundVertexBuffer = mesh->VertexBuffer;
				}

				if (mesh->IndexBuffer != boundIndexBuffer) {
					SDL_GPUBufferBinding indexBinding{.buffer = mesh->IndexBuffer, .offset = 0};
					SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
					boundIndexBuffer = mesh->IndexBuffer;
				}

				SDL_DrawGPUIndexedPrimitives(pass, mesh->IndexCount, 1, 0, 0, 0);

					if (measuring) {
					// Names are attached after indexed collection.
					auto *primitive = part->Cast<Part>();
					auto index = magic_enum::enum_index(primitive ? primitive->GetShape() : Enums::PartType::Block);
					if (index && *index < shapeDraws.size()) {
						shapeDraws[*index]++;
						shapeTriangles[*index] += mesh->IndexCount / 3;
					}
				}

				if (measuring) {
					uint64_t partEnd = SDL_GetTicksNS();
					transformNanoseconds += partSubmit - partStart;
					submitNanoseconds += partEnd - partSubmit;
				}
				partsDrawn++;
				individualParts++;
			}

			if (measuring) {
				// Published once, with the count beside it for per-part cost.
				if (individualParts > 0) {
					ProfilerCountTime("Individual Transforms ms", transformNanoseconds);
					ProfilerCountTime("Individual Submit ms", submitNanoseconds);
					ProfilerCount("Individual Parts", individualParts);
				}
			}
			if (measuring) {
				for (size_t index = 0; index < shapeDraws.size(); index++) {
					if (shapeDraws[index] == 0) {
						continue;
					}
					const auto &names = CounterNames(magic_enum::enum_values<Enums::PartType>()[index]);
					// Held by a function-local static, so the pointers Tracy
					// keeps stay good for the life of the process
					ProfilerCount(names.first.c_str(), shapeDraws[index]);
					ProfilerCount(names.second.c_str(), shapeTriangles[index]);
				}
				uint64_t totalTriangles = 0;
				for (uint64_t count : shapeTriangles) {
					totalTriangles += count;
				}

				ProfilerCount("Parts Drawn", partsDrawn);
				ProfilerCount("Triangles", totalTriangles);
			}

			return pass;
		};

	  private:
		// Format once; per-part formatting added two allocations to measured work.
		static const std::pair<std::string, std::string> &CounterNames(Enums::PartType shape) {
			static const std::vector<std::pair<std::string, std::string>> NAMES = [] {
				std::vector<std::pair<std::string, std::string>> names;
				for (auto value : magic_enum::enum_values<Enums::PartType>()) {
					std::string base(magic_enum::enum_name(value));
					names.emplace_back(base + " Draws", base + " Tris");
				}
				return names;
			}();
			static const std::pair<std::string, std::string> OTHER{"Other Draws", "Other Tris"};

			auto index = magic_enum::enum_index(shape);
			return index && *index < NAMES.size() ? NAMES[*index] : OTHER;
		}


	};

	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
		return std::make_unique<OpaquePass>(gpu, swapchainFormat);
	}
} // namespace gargantuan
