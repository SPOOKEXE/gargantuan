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
	static const glm::mat4 SHADOW_BIAS_MATRIX{
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
			glm::mat4 ShadowBiasMatrix;
			glm::vec4 LightDirection;
		};

		struct alignas(16) PartUniforms {
			glm::mat4 ModelMatrix;
			glm::vec4 Color;
		};

		struct alignas(16) PartFragmentUniforms {
			glm::vec4 HasSurfaceTexture;
			// xyz is the textured face's world-space normal.
			glm::vec4 SurfaceNormal;
			// xy tiles; zw offsets.
			glm::vec4 SurfaceTransform;
		};

		// Matches the std430 block the instanced vertex stage reads
		FileShader InstancedShader{
			.VertexFilepath = GetShaderPath("opaque_instanced.vert"),
			.VertexUniformBufferCount = 1,
			.VertexStorageBufferCount = 1,
			.FragmentFilepath = GetShaderPath("opaque_instanced.frag"),
			.FragmentUniformBufferCount = 2,
			.FragmentSamplerCount = 2,
		};
		SDL_GPUGraphicsPipeline *InstancedPipeline = nullptr;

		SDL_GPUBuffer *InstanceBuffer = nullptr;
		SDL_GPUTransferBuffer *InstanceTransfer = nullptr;
		uint32_t InstanceCapacity = 0;
		// Retained to avoid reallocating large instance sets.
		std::vector<InstanceData> InstanceScratch;
		struct Batch {
			GpuMesh *Mesh = nullptr;
			// Texture is bound once per mesh-texture batch.
			SDL_GPUTexture *Texture = nullptr;
			Enums::PartType Shape = Enums::PartType::Block;
			uint32_t First = 0;
			uint32_t Count = 0;
		};
		std::vector<Batch> Batches;
		// Visible-list index to batch index.
		static constexpr uint32_t SKIPPED = 0xFFFFFFFFu;
		std::vector<uint32_t> PartBatch;
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

		FileShader Shader{
			.VertexFilepath = GetShaderPath("opaque.vert"),
			.VertexUniformBufferCount = 2,
			.FragmentFilepath = GetShaderPath("opaque.frag"),
			.FragmentUniformBufferCount = 2,
			.FragmentSamplerCount = 2,
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
				.size = capacity * (uint32_t)sizeof(InstanceData),
			};
			InstanceBuffer = SDL_CreateGPUBuffer(gpu, &bufferInfo);

			SDL_GPUTransferBufferCreateInfo transferInfo{
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = capacity * (uint32_t)sizeof(InstanceData),
			};
			InstanceTransfer = SDL_CreateGPUTransferBuffer(gpu, &transferInfo);

			InstanceCapacity = InstanceBuffer && InstanceTransfer ? capacity : 0;
			return InstanceCapacity > 0;
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
			if (PartBatch.size() < parts.size()) {
				PartBatch.resize(parts.size());
			}

			const bool useSlots = context.SurfaceTextures && context.SurfaceSlotsComplete;
			SDL_GPUTexture *const *surfaceTextures = useSlots ? context.SurfaceTextures->data() : nullptr;
			const uint32_t surfaceTextureCount = useSlots ? (uint32_t)context.SurfaceTextures->size() : 0;

			BasePart *const *partList = parts.Data;
			uint32_t *partBatchOut = PartBatch.data();
			Batch *batches = Batches.data();
			const size_t count = parts.size();

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
				BasePart *part = partList[index];
				uint32_t meshId = part->MeshId;
				uint32_t bucket = SKIPPED;

				if (useSlots && meshId != 0 && meshId < MAX_MESH_IDS) {
					uint32_t key = meshId * MAX_SURFACE_SLOTS + part->SurfaceTextureSlot;
					bucket = KeyToBatch[key];
					if (bucket == SKIPPED) {
						uint32_t slot = part->SurfaceTextureSlot;
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
					SDL_GPUTexture *texture = context.WhiteTexture;
					if (anyPartTextures && (part->SurfaceCamera || part->SurfaceImage)) {
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
			uint32_t running = 0;
			for (Batch &batch : Batches) {
				batch.First = running;
				running += batch.Count;
			}

			// Retained to avoid repeat allocation.
			Cursors.resize(Batches.size());
			for (size_t index = 0; index < Batches.size(); index++) {
				Cursors[index] = Batches[index].First;
			}

			SDL_GPUTexture *White = context.WhiteTexture;

			// Cache vector storage used repeatedly per part.
			uint32_t *cursors = Cursors.data();
			InstanceData *scratch = InstanceScratch.data();

			for (size_t index = 0; index < count; index++) {
				uint32_t bucket = partBatchOut[index];
				if (bucket == SKIPPED) {
					continue;
				}

				BasePart *part = partList[index];
				InstanceData &instance = scratch[cursors[bucket]++];

				//   T * R * S  =  [ r0*sx  r1*sy  r2*sz  position ]
				//
				// Plain floats through raw pointers: every glm operator[] and
				// every accessor is a call at -O0, including the two that
				// reaching &mat3[0][0] costs, and this runs 370k times a pass.
				// mat3 columns are contiguous, so mat3[c][r] is rot[c * 3 + r].
				const float *rotation = reinterpret_cast<const float *>(&part->CFrame.Rotation);
				const glm::vec3 &size = part->Size;
				const glm::vec3 &position = part->CFrame.Position;
				float *model = reinterpret_cast<float *>(&instance.ModelMatrix);

				model[0] = rotation[0] * size.x;
				model[1] = rotation[1] * size.x;
				model[2] = rotation[2] * size.x;
				model[3] = 0.0f;
				model[4] = rotation[3] * size.y;
				model[5] = rotation[4] * size.y;
				model[6] = rotation[5] * size.y;
				model[7] = 0.0f;
				model[8] = rotation[6] * size.z;
				model[9] = rotation[7] * size.z;
				model[10] = rotation[8] * size.z;
				model[11] = 0.0f;
				model[12] = position.x;
				model[13] = position.y;
				model[14] = position.z;
				model[15] = 1.0f;

				const Color3 &colour = part->Color;
				instance.Color.x = colour.R;
				instance.Color.y = colour.G;
				instance.Color.z = colour.B;
				instance.Color.w = 1.0f - part->Transparency;

				// Skip unused surface transforms when the shader flag is off.
				if (batches[bucket].Texture == White) {
					continue;
				}

				// The transform the vertex stage puts the mesh's normals
				// through, not the correct inverse transpose. Wrong the same
				// way on both sides, which is all a match needs.
				glm::vec4 match = part->GetSurfaceMatch();
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

				instance.SurfaceNormal.x = nx;
				instance.SurfaceNormal.y = ny;
				instance.SurfaceNormal.z = nz;
				instance.SurfaceNormal.w = match.w;
				instance.SurfaceTransform.x = part->SurfaceTiling.Value.x;
				instance.SurfaceTransform.y = part->SurfaceTiling.Value.y;
				instance.SurfaceTransform.z = part->SurfaceOffset.Value.x;
				instance.SurfaceTransform.w = part->SurfaceOffset.Value.y;
			}

			InstanceFillNanoseconds = SDL_GetTicksNS() - fillStarted;
			uint64_t uploadStarted = SDL_GetTicksNS();

			if (!EnsureInstanceCapacity(gpu, running)) {
				InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
				return false;
			}

			void *mapped = SDL_MapGPUTransferBuffer(gpu, InstanceTransfer, true);
			if (!mapped) {
				InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
				return false;
			}
			std::memcpy(mapped, InstanceScratch.data(), running * sizeof(InstanceData));
			SDL_UnmapGPUTransferBuffer(gpu, InstanceTransfer);

			SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(context.Commands);
			SDL_GPUTransferBufferLocation source{.transfer_buffer = InstanceTransfer, .offset = 0};
			SDL_GPUBufferRegion destination{
				.buffer = InstanceBuffer, .offset = 0, .size = running * (uint32_t)sizeof(InstanceData)
			};
			// Cycle transfer storage to avoid waiting on the prior frame.
			SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
			SDL_EndGPUCopyPass(copyPass);

			InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
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

			Shader.Init(gpu);
			Pipeline = PipelineBuilder()
						   .SetVertexShader(Shader.VertexShader)
						   .SetFragmentShader(Shader.FragmentShader)
						   .SetColorEnabled(true)
						   .SetColorFormat(swapchainFormat)
						   .SetBlendingEnabled(true)
						   .SetDepthEnabled(true)
						   .SetDepthFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
						   .Build(gpu);
		};

		SDL_GPURenderPass *Draw(SDL_GPUDevice *gpu, FrameContext &context) override {
			// Read counters once outside the measured per-part path.
			Profiler *profiler = Profiler::GetCurrent();
			const bool measuring = profiler && profiler->IsEnabled();
			uint64_t transformNanoseconds = 0;
			uint64_t submitNanoseconds = 0;
			uint64_t partsDrawn = 0;
			// Track unbatched parts separately.
			uint64_t individualParts = 0;

			// Publish once; per-part updates scanned counter names twice.
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeDraws{};
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeTriangles{};

			// Instance upload must finish before opening the render pass.
			std::vector<BasePart *> everything;
			PartSpan drawList;
			if (context.Visible) {
				drawList = context.Visible->InViewParts();
			} else {
				everything = context.WorldRoot->RawParts;
				drawList = {everything.data(), everything.size()};
			}

			const bool anyPartTextures = context.PartTextures && !context.PartTextures->empty();

			// One draw per mesh-texture pair; SurfaceShader stays per-part.
			bool instanced = InstancedPipeline && !context.SurfacePipeline && drawList.size() > 0 &&
				PrepareInstances(gpu, context, drawList, anyPartTextures);

			SDL_GPUColorTargetInfo colorTarget = {
				.texture = context.ColorTarget,
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
				.ShadowBiasMatrix = SHADOW_BIAS_MATRIX * context.ShadowMatrix,
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
				SDL_BindGPUVertexStorageBuffers(pass, 0, &InstanceBuffer, 1);

				uint64_t submitStart = measuring ? SDL_GetTicksNS() : 0;
				for (const Batch &batch : Batches) {
					// All instances in a batch share texture presence.
					PartFragmentUniforms shared{
						.HasSurfaceTexture =
							glm::vec4(batch.Texture != context.WhiteTexture ? 1.0f : 0.0f, 0, 0, 0),
						.SurfaceNormal = glm::vec4(0.0f),
						.SurfaceTransform = glm::vec4(0.0f),
					};
					SDL_PushGPUFragmentUniformData(
						context.Commands, 1, &shared, sizeof(PartFragmentUniforms)
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
						pass, batch.Mesh->IndexCount, batch.Count, 0, 0, batch.First
					);
					partsDrawn += batch.Count;
				}

				if (measuring) {
					submitNanoseconds += SDL_GetTicksNS() - submitStart;
					profiler->AddZoneTime("Bucket", InstanceBucketNanoseconds, 1);
					profiler->AddZoneTime("Fill Instances", InstanceFillNanoseconds, (uint64_t)drawList.size());
					profiler->AddZoneTime("Upload", InstanceUploadNanoseconds, 1);
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
					.Color = glm::vec4((glm::vec3)part->Color, 1.0f - part->Transparency),
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
						if (textured) {
							surfaceMatch = part->GetSurfaceMatch();
							surfaceNormal = glm::vec3(surfaceMatch);
							if (glm::dot(surfaceNormal, surfaceNormal) > 0.0f) {
								surfaceNormal =
									glm::normalize(glm::mat3(uniforms.ModelMatrix) * surfaceNormal);
							}
						}

						PartFragmentUniforms fragmentUniforms{
							.HasSurfaceTexture = glm::vec4(textured ? 1.0f : 0.0f, 0, 0, 0),
							.SurfaceNormal = glm::vec4(surfaceNormal, surfaceMatch.w),
							.SurfaceTransform = glm::vec4(
								part->SurfaceTiling.GetX(),
								part->SurfaceTiling.GetY(),
								part->SurfaceOffset.GetX(),
								part->SurfaceOffset.GetY()
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
				// Publish once with part count for per-part cost.
				if (individualParts > 0) {
					profiler->AddZoneTime("Individual Transforms", transformNanoseconds, individualParts);
					profiler->AddZoneTime("Individual Submit", submitNanoseconds, individualParts);
				}
			}
			if (measuring) {
				for (size_t index = 0; index < shapeDraws.size(); index++) {
					if (shapeDraws[index] == 0) {
						continue;
					}
					const auto &names = CounterNames(magic_enum::enum_values<Enums::PartType>()[index]);
					profiler->Add(names.first, shapeDraws[index]);
					profiler->Add(names.second, shapeTriangles[index]);
				}
				uint64_t totalTriangles = 0;
				for (uint64_t count : shapeTriangles) {
					totalTriangles += count;
				}

				profiler->Add("Parts Drawn", partsDrawn);
				profiler->Add("Triangles", totalTriangles);
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
