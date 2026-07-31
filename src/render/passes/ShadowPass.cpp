#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/Profiler.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <memory>
#include <vector>

namespace gargantuan {

	class ShadowPass final : public RenderPass {
	  public:
		struct alignas(16) PerCasterUniforms {
			glm::mat4 ShadowMatrix;
			glm::mat4 ModelMatrix;
		};

		// Instanced, and position only. The old path pushed a 128-byte uniform and
		// issued a draw per caster; this pushes one matrix for the pass and draws
		// once per mesh. Depth-only means nothing here reads a normal or a uv, so
		// the pipeline is built for a single 12-byte stream instead of the
		// interleaved 32-byte vertex -- and GpuMesh already had the split buffers,
		// nothing was using them.
		struct alignas(16) InstancedUniforms {
			glm::mat4 ShadowMatrix;
		};

		FileShader InstancedShader{
			.VertexFilepathStem = GetShaderPath("shadow_instanced.vert"),
			.VertexUniformBufferCount = 1,
			// The casters' instances, then the indices that select them.
			.VertexStorageBufferCount = 2,
			.FragmentFilepathStem = GetShaderPath("shadow.frag"),
			.FragmentUniformBufferCount = 0,
		};
		SDL_GPUGraphicsPipeline *InstancedPipeline = nullptr;

		// The casters, in batch order. Compact rather than the whole scene indexed
		// by slot: the caster set is not the visible set, and a second scene-sized
		// buffer to save a copy of a few hundred casters is the wrong way round.
		SDL_GPUBuffer *InstanceBuffer = nullptr;
		SDL_GPUTransferBuffer *InstanceTransfer = nullptr;
		SDL_GPUBuffer *InstanceIndexBuffer = nullptr;
		SDL_GPUTransferBuffer *InstanceIndexTransfer = nullptr;
		uint32_t InstanceCapacity = 0;
		std::vector<PartInstance> InstanceScratch;
		std::vector<uint32_t> InstanceIndexScratch;

		struct Batch {
			GpuMesh *Mesh = nullptr;
			uint32_t First = 0;
			uint32_t Count = 0;
		};
		std::vector<Batch> Batches;

		ShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
			PassShader = FileShader{
				.VertexFilepathStem = GetShaderPath("shadow.vert"),
				.VertexUniformBufferCount = 1,
				.FragmentFilepathStem = GetShaderPath("shadow.frag"),
				.FragmentUniformBufferCount = 0,
			};
			PassShader.Init(gpu);

			Pipeline = PipelineBuilder()
						   .SetVertexShader(PassShader.VertexShader)
						   .SetFragmentShader(PassShader.FragmentShader)
						   .SetColorEnabled(false)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
						   .Build(gpu);

			InstancedShader.Init(gpu);
			InstancedPipeline = PipelineBuilder()
									.SetVertexShader(InstancedShader.VertexShader)
									.SetFragmentShader(InstancedShader.FragmentShader)
									.SetColorEnabled(false)
									.SetDepthEnabled(true)
									.SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
									.SetVertexStreams(VertexStreams::Position)
									.Build(gpu);
		};

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
			if (InstanceTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceTransfer);
				InstanceTransfer = nullptr;
			}
			if (InstanceIndexTransfer) {
				SDL_ReleaseGPUTransferBuffer(gpu, InstanceIndexTransfer);
				InstanceIndexTransfer = nullptr;
			}
			InstanceCapacity = 0;
			InstancedShader.Destroy(gpu);
			RenderPass::Destroy(gpu);
		}

		bool EnsureCapacity(SDL_GPUDevice *gpu, uint32_t count) {
			if (count <= InstanceCapacity && InstanceBuffer && InstanceTransfer && InstanceIndexBuffer &&
				InstanceIndexTransfer) {
				return true;
			}

			uint32_t capacity = InstanceCapacity ? InstanceCapacity : 512;
			while (capacity < count) {
				capacity *= 2;
			}

			if (InstanceBuffer) SDL_ReleaseGPUBuffer(gpu, InstanceBuffer);
			if (InstanceTransfer) SDL_ReleaseGPUTransferBuffer(gpu, InstanceTransfer);
			if (InstanceIndexBuffer) SDL_ReleaseGPUBuffer(gpu, InstanceIndexBuffer);
			if (InstanceIndexTransfer) SDL_ReleaseGPUTransferBuffer(gpu, InstanceIndexTransfer);

			SDL_GPUBufferCreateInfo instanceInfo{
				.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
				.size = capacity * (uint32_t)sizeof(PartInstance),
			};
			InstanceBuffer = SDL_CreateGPUBuffer(gpu, &instanceInfo);
			SDL_GPUTransferBufferCreateInfo instanceTransferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = capacity * (uint32_t)sizeof(PartInstance),
			};
			InstanceTransfer = SDL_CreateGPUTransferBuffer(gpu, &instanceTransferInfo);

			SDL_GPUBufferCreateInfo indexInfo{
				.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
				.size = capacity * (uint32_t)sizeof(uint32_t),
			};
			InstanceIndexBuffer = SDL_CreateGPUBuffer(gpu, &indexInfo);
			SDL_GPUTransferBufferCreateInfo indexTransferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = capacity * (uint32_t)sizeof(uint32_t),
			};
			InstanceIndexTransfer = SDL_CreateGPUTransferBuffer(gpu, &indexTransferInfo);

			InstanceCapacity =
				InstanceBuffer && InstanceTransfer && InstanceIndexBuffer && InstanceIndexTransfer ? capacity : 0;
			return InstanceCapacity > 0;
		}

		// Buckets the casters by mesh and fills the two buffers. False means draw
		// the old way -- no slots recorded, nothing to draw, or a GPU refusal.
		bool PrepareCasters(SDL_GPUDevice *gpu, FrameContext &context, PartSpan casters) {
			Batches.clear();
			if (!context.VisibleParts || !context.InstancesBySlot || casters.size() == 0) {
				return false;
			}

			const uint32_t *slots = context.VisibleParts->ShadowSlots();
			const std::vector<PartInstance> &rows = *context.InstancesBySlot;
			if (!slots) {
				return false;
			}

			const size_t count = casters.size();
			if (InstanceScratch.size() < count) {
				InstanceScratch.resize(count);
				InstanceIndexScratch.resize(count);
			}

			// Bucket by mesh. A handful of primitive meshes, so a linear search over
			// the open batches beats a map at this width.
			if (BatchIndexPerPart.size() < count) {
				BatchIndexPerPart.resize(count);
			}
			for (size_t index = 0; index < count; index++) {
				BasePart *part = casters.Parts[index];
				auto &mesh = part->GetMesh();
				BatchIndexPerPart[index] = SKIPPED;
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}
				uint32_t found = SKIPPED;
				for (uint32_t candidate = 0; candidate < (uint32_t)Batches.size(); candidate++) {
					if (Batches[candidate].Mesh == mesh.get()) {
						found = candidate;
						break;
					}
				}
				if (found == SKIPPED) {
					found = (uint32_t)Batches.size();
					Batches.push_back({mesh.get(), 0, 0});
				}
				BatchIndexPerPart[index] = found;
				Batches[found].Count++;
			}

			if (Batches.empty()) {
				return false;
			}

			uint32_t running = 0;
			Cursors.resize(Batches.size());
			for (size_t index = 0; index < Batches.size(); index++) {
				Batches[index].First = running;
				Cursors[index] = running;
				running += Batches[index].Count;
			}

			for (size_t index = 0; index < count; index++) {
				uint32_t bucket = BatchIndexPerPart[index];
				if (bucket == SKIPPED) {
					continue;
				}
				uint32_t slot = slots[index];
				if (slot >= rows.size()) {
					return false;
				}
				uint32_t at = Cursors[bucket]++;
				InstanceScratch[at] = rows[slot];
				// Compact and already in batch order, so the indirection the shader
				// insists on is the identity here.
				InstanceIndexScratch[at] = at;
			}

			if (!EnsureCapacity(gpu, running)) {
				return false;
			}

			void *mappedInstances = SDL_MapGPUTransferBuffer(gpu, InstanceTransfer, true);
			if (!mappedInstances) {
				return false;
			}
			std::memcpy(mappedInstances, InstanceScratch.data(), running * sizeof(PartInstance));
			SDL_UnmapGPUTransferBuffer(gpu, InstanceTransfer);

			void *mappedIndices = SDL_MapGPUTransferBuffer(gpu, InstanceIndexTransfer, true);
			if (!mappedIndices) {
				return false;
			}
			std::memcpy(mappedIndices, InstanceIndexScratch.data(), running * sizeof(uint32_t));
			SDL_UnmapGPUTransferBuffer(gpu, InstanceIndexTransfer);

			SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(context.Commands);
			SDL_GPUTransferBufferLocation instanceSource{.transfer_buffer = InstanceTransfer, .offset = 0};
			SDL_GPUBufferRegion instanceDestination{
				.buffer = InstanceBuffer, .offset = 0, .size = running * (uint32_t)sizeof(PartInstance)
			};
			SDL_UploadToGPUBuffer(copyPass, &instanceSource, &instanceDestination, true);

			SDL_GPUTransferBufferLocation indexSource{.transfer_buffer = InstanceIndexTransfer, .offset = 0};
			SDL_GPUBufferRegion indexDestination{
				.buffer = InstanceIndexBuffer, .offset = 0, .size = running * (uint32_t)sizeof(uint32_t)
			};
			SDL_UploadToGPUBuffer(copyPass, &indexSource, &indexDestination, true);
			SDL_EndGPUCopyPass(copyPass);

			return true;
		}

		static constexpr uint32_t SKIPPED = 0xFFFFFFFFu;
		uint32_t LoggedFrames = 0;
		std::vector<uint32_t> BatchIndexPerPart;
		std::vector<uint32_t> Cursors;

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			glm::mat4 shadowProjection = glm::ortho<float>(
				-SHADOW_ORTHO_EXTENT, SHADOW_ORTHO_EXTENT, -SHADOW_ORTHO_EXTENT, SHADOW_ORTHO_EXTENT,
				SHADOW_ORTHO_NEAR, SHADOW_ORTHO_FAR
			);
			glm::vec3 lightPosition = glm::normalize(context.LightDirection) * SHADOW_EYE_DISTANCE;
			glm::mat4 shadowView = glm::lookAt(lightPosition, glm::vec3(0), glm::vec3(0, 1, 0));
			glm::mat4 shadowMatrix = shadowProjection * shadowView;
			context.LightViewProjectionMatrix = shadowMatrix;

			SDL_GPUDepthStencilTargetInfo depthTarget{
				.texture = context.ShadowMapTexture,
				.clear_depth = 1.0f,
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
				.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
				.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
			};

			// Includes offscreen casters whose shadows reach view; already filtered.
			std::vector<BasePart *> allShadowCasters;
			PartSpan castList;
			if (context.VisibleParts) {
				castList = context.VisibleParts->ShadowParts();
			} else {
				allShadowCasters.reserve(context.WorldRoot->Parts.Raw().size());
				for (BasePart *candidate : context.WorldRoot->Parts.Raw()) {
					if (candidate->Visual.CastShadow) {
						allShadowCasters.push_back(candidate);
					}
				}
				castList = {allShadowCasters.data(), allShadowCasters.size()};
			}

			// The copy pass has to be recorded before the render pass opens: a copy
			// cannot be started inside one. This is also why the upload lives here
			// and not further down beside the draws that read it.
			const bool instanced = InstancedPipeline && PrepareCasters(gpu, context, castList);

			SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(context.Commands, nullptr, 0, &depthTarget);

			if (instanced) {
				SDL_BindGPUGraphicsPipeline(pass, InstancedPipeline);
				InstancedUniforms uniforms{.ShadowMatrix = shadowMatrix};
				SDL_PushGPUVertexUniformData(context.Commands, 0, &uniforms, sizeof(InstancedUniforms));

				SDL_GPUBuffer *storage[2] = {InstanceBuffer, InstanceIndexBuffer};
				SDL_BindGPUVertexStorageBuffers(pass, 0, storage, 2);

				uint32_t draws = 0;
				for (const Batch &batch : Batches) {
					if (!batch.Mesh || batch.Count == 0) {
						continue;
					}

					// Position alone, matching the layout the pipeline was built
					// with. A pipeline expecting one 12-byte stream cannot read the
					// 32-byte interleaved buffer, so a mesh without the split
					// streams is skipped rather than drawn as rubbish.
					SDL_GPUBuffer *streams[3] = {};
					uint32_t streamCount = batch.Mesh->CollectStreamBuffers(VertexStreams::Position, streams, 3);
					if (streamCount == 0) {
						continue;
					}
					SDL_GPUBufferBinding vertexBindings[3] = {};
					for (uint32_t index = 0; index < streamCount; index++) {
						vertexBindings[index] = {.buffer = streams[index], .offset = 0};
					}
					SDL_BindGPUVertexBuffers(pass, 0, vertexBindings, streamCount);

					SDL_GPUBufferBinding indexBinding{.buffer = batch.Mesh->IndexBuffer, .offset = 0};
					SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

					SDL_DrawGPUIndexedPrimitives(
						pass, batch.Mesh->IndexCount, batch.Count, 0, 0, batch.First
					);
					draws++;
				}

				ProfilerCount("v1.shadow.draws", draws);
				ProfilerCount("v1.shadow.casters", castList.size());
				// Log initial batch reduction before profiler attachment.
				if (LoggedFrames < 3) {
					LoggedFrames++;
					SDL_Log("v1 shadow: %u draws, %zu casters, position stream", draws, castList.size());
				}
				return pass;
			}

			SDL_BindGPUGraphicsPipeline(pass, Pipeline);

			SDL_GPUBuffer *boundVertexBuffer = nullptr;
			SDL_GPUBuffer *boundIndexBuffer = nullptr;

			ProfilerCount("v1.shadow.draws", castList.size());
			ProfilerCount("v1.shadow.casters", castList.size());
			if (LoggedFrames < 3) {
				LoggedFrames++;
				SDL_Log("v1 shadow: %zu draws, %zu casters, interleaved (not instanced)",
					castList.size(), castList.size());
			}

			for (BasePart *part : castList) {
				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				PerCasterUniforms uniforms{.ShadowMatrix = shadowMatrix, .ModelMatrix = part->GetModelMatrix()};
				SDL_PushGPUVertexUniformData(context.Commands, 0, &uniforms, sizeof(PerCasterUniforms));

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
			}

			return pass;
		};
	};

	std::unique_ptr<RenderPass> CreateShadowPass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
		return std::make_unique<ShadowPass>(gpu, swapchainFormat);
	}

} // namespace gargantuan
