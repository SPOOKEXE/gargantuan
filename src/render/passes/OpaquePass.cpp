#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
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
		//
		0.5f,
		0.0f,
		0.0f,
		0.0f,
		//
		0.0f,
		-0.5f,
		0.0f,
		0.0f,
		//
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		//
		0.5f,
		0.5f,
		0.0f,
		1.0f
	};

	namespace {
		const std::vector<BasePart *> EMPTY_PARTS;
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
			// xyz is the face the picture lands on, in world space, so the
			// fragment stage can tell which face it is shading without needing
			// the part's own transform
			glm::vec4 SurfaceNormal;
			// xy tiles the picture across that face, zw slides it
			glm::vec4 SurfaceTransform;
		};

		// Matches the std430 block the instanced vertex stage reads
		struct alignas(16) InstanceData {
			glm::mat4 ModelMatrix;
			glm::vec4 Color;
			// Per instance because which face the picture lands on depends on
			// how the part is turned. Whether there is one at all is per batch.
			glm::vec4 SurfaceNormal;
			glm::vec4 SurfaceTransform;
		};

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
		// Held between frames: a hundred thousand instances is one allocation
		std::vector<InstanceData> InstanceScratch;
		struct Batch {
			GpuMesh *Mesh = nullptr;
			// Batched on the texture as well as the mesh, because the texture
			// is bound once for the whole batch
			SDL_GPUTexture *Texture = nullptr;
			Enums::PartType Shape = Enums::PartType::Block;
			uint32_t First = 0;
			uint32_t Count = 0;
		};
		std::vector<Batch> Batches;
		// Parallel to the visible list: which batch each part went into
		static constexpr uint32_t SKIPPED = 0xFFFFFFFFu;
		std::vector<uint32_t> PartBatch;
		std::vector<uint32_t> Cursors;
		// Grouped by mesh, so a part only scans its own shape's textures
		std::vector<GpuMesh *> MeshSlots;
		std::vector<std::vector<uint32_t>> MeshBuckets;
		// MeshId -> slot, so a part finds its mesh without asking through a
		// virtual call. Only the first part of each shape has to ask.
		std::array<uint32_t, 256> MeshIdSlots;
		// Timed at their own boundaries rather than per part
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

		// Doubling, so a steady scene stops reallocating after its first frame
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

		// Buckets by mesh and texture, writes one buffer, uploads it. False
		// when there is nothing to do or the buffer could not be had.
		bool PrepareInstances(
			SDL_GPUDevice *gpu,
			FrameContext &context,
			const std::vector<BasePart *> &parts,
			bool anyPartTextures
		) {
			InstanceBucketNanoseconds = 0;
			InstanceFillNanoseconds = 0;
			InstanceUploadNanoseconds = 0;
			uint64_t started = SDL_GetTicksNS();

			Batches.clear();
			MeshSlots.clear();
			MeshIdSlots.fill(SKIPPED);
			for (auto &list : MeshBuckets) {
				list.clear();
			}
			MeshBuckets.clear();
			// Never cleared: every element is written before it is read, so
			// zeroing first is a nine megabyte memset for nothing
			if (InstanceScratch.size() < parts.size()) {
				InstanceScratch.resize(parts.size());
			}

			// A handful of meshes, so a linear scan finds the bucket faster
			// than hashing would. Parts of a shape end up contiguous, which is
			// what lets each shape be one draw.
			// Remembered so the second pass does not ask for the mesh and scan
			// the buckets all over again
			PartBatch.clear();
			PartBatch.reserve(parts.size());
			for (BasePart *part : parts) {
				// Two pointer tests before the map, which holds only the parts
				// that show something but was asked about all of them
				SDL_GPUTexture *texture = context.WhiteTexture;
				if (anyPartTextures && (part->SurfaceCamera || part->SurfaceImage)) {
					auto found = context.PartTextures->find(part);
					if (found != context.PartTextures->end() && found->second) {
						texture = found->second;
					}
				}

				// Mesh first, then the texture within it. One flat scan grew from
				// five entries to twenty five when textures arrived.
				//
				// Parts sharing a MeshId share a mesh, so only the first of each
				// shape asks for it -- the rest are one array read.
				uint8_t meshId = part->MeshId;
				uint32_t meshSlot = meshId != 0 ? MeshIdSlots[meshId] : SKIPPED;
				if (meshSlot == SKIPPED) {
					auto &mesh = part->GetMesh();
					if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
						PartBatch.push_back(SKIPPED);
						continue;
					}

					for (uint32_t candidate = 0; candidate < (uint32_t)MeshSlots.size(); candidate++) {
						if (MeshSlots[candidate] == mesh.get()) {
							meshSlot = candidate;
							break;
						}
					}
					if (meshSlot == SKIPPED) {
						meshSlot = (uint32_t)MeshSlots.size();
						MeshSlots.push_back(mesh.get());
						MeshBuckets.emplace_back();
					}
					if (meshId != 0) {
						MeshIdSlots[meshId] = meshSlot;
					}
				}

				uint32_t bucket = SKIPPED;
				for (uint32_t candidate : MeshBuckets[meshSlot]) {
					if (Batches[candidate].Texture == texture) {
						bucket = candidate;
						break;
					}
				}
				if (bucket == SKIPPED) {
					auto *primitive = part->Cast<Part>();
					bucket = (uint32_t)Batches.size();
					Batches.push_back(
						{MeshSlots[meshSlot], texture, primitive ? primitive->GetShape() : Enums::PartType::Block, 0, 0}
					);
					MeshBuckets[meshSlot].push_back(bucket);
				}

				PartBatch.push_back(bucket);
				Batches[bucket].Count++;
			}

			InstanceBucketNanoseconds = SDL_GetTicksNS() - started;
			if (Batches.empty()) {
				return false;
			}

			uint64_t fillStarted = SDL_GetTicksNS();

			// Where each run starts, so the second pass writes straight in
			uint32_t running = 0;
			for (Batch &batch : Batches) {
				batch.First = running;
				running += batch.Count;
			}

			// Held between frames as well, for the same reason
			Cursors.resize(Batches.size());
			for (size_t index = 0; index < Batches.size(); index++) {
				Cursors[index] = Batches[index].First;
			}

			SDL_GPUTexture *White = context.WhiteTexture;

			// Raw pointers, because every one of these was a subscript call on
			// a vector and five of them ran per part
			const uint32_t *partBuckets = PartBatch.data();
			BasePart *const *partList = parts.data();
			uint32_t *cursors = Cursors.data();
			InstanceData *scratch = InstanceScratch.data();
			const Batch *batches = Batches.data();

			const size_t count = parts.size();
			for (size_t index = 0; index < count; index++) {
				uint32_t bucket = partBuckets[index];
				if (bucket == SKIPPED) {
					continue;
				}

				BasePart *part = partList[index];
				InstanceData &instance = scratch[cursors[bucket]++];

				// Written straight into the slot in plain floats. The same
				// arithmetic as GetModelMatrix without the glm temporaries,
				// which are calls until something inlines them. The rotation
				// is read through a float pointer for the same reason: its
				// columns are contiguous, so mat3[c][r] is rotation[c * 3 + r].
				//
				//   T * R * S  =  [ r0*sx  r1*sy  r2*sz  position ]
				const float *rotation = &part->CFrame.Rotation[0][0];
				const glm::vec3 &size = part->Size;
				const glm::vec3 &position = part->CFrame.Position;
				float *model = &instance.ModelMatrix[0][0];

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

				// The shader reads these behind a flag that is off for the
				// rest, so a matrix multiply and a normalise for nothing
				if (batches[bucket].Texture == White) {
					continue;
				}

				// The same transform the vertex stage puts the mesh's normals
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
				instance.SurfaceTransform.x = part->SurfaceTiling.GetX();
				instance.SurfaceTransform.y = part->SurfaceTiling.GetY();
				instance.SurfaceTransform.z = part->SurfaceOffset.GetX();
				instance.SurfaceTransform.w = part->SurfaceOffset.GetY();
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
			// Cycled, so this upload does not wait on the GPU still reading
			// last frame's copy
			SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
			SDL_EndGPUCopyPass(copyPass);

			InstanceUploadNanoseconds = SDL_GetTicksNS() - uploadStarted;
			return true;
		}

		OpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
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
			// Read once: asking per part is not something a measurement should
			// do to the thing it measures
			Profiler *profiler = Profiler::GetCurrent();
			const bool measuring = profiler && profiler->IsEnabled();
			uint64_t transformNanoseconds = 0;
			uint64_t submitNanoseconds = 0;
			uint64_t partsDrawn = 0;
			// The ones that went through one at a time, counted apart from the
			// batched ones so the chart can price them separately
			uint64_t individualParts = 0;

			// Handed over once at the end. Per part this was a linear scan of
			// the counter list with a string compare at every step, twice.
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeDraws{};
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeTriangles{};

			// Decided before the pass opens: the instance upload is a copy
			// pass, and a copy pass cannot run inside a render pass
			std::vector<BasePart *> everything;
			const std::vector<BasePart *> *drawList = nullptr;
			if (context.Visible) {
				drawList = &context.Visible->InViewList;
			} else {
				everything.reserve(context.WorldRoot->Parts.size());
				for (const auto &candidate : context.WorldRoot->Parts) {
					if (candidate) {
						everything.push_back(candidate.get());
					}
				}
				drawList = &everything;
			}

			const bool anyPartTextures = context.PartTextures && !context.PartTextures->empty();

			// One draw per shape and texture rather than one per part. A
			// SurfaceShader has taken the fragment stage over and keeps the
			// per-part path.
			bool instanced = InstancedPipeline && !context.SurfacePipeline && !drawList->empty() &&
				PrepareInstances(gpu, context, *drawList, anyPartTextures);

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
			// A camera's SurfaceShader swaps the fragment stage for its own
			SDL_BindGPUGraphicsPipeline(
				pass,
				instanced ? InstancedPipeline : (context.SurfacePipeline ? context.SurfacePipeline : Pipeline)
			);

			// A surface shader may sample its own images after the shadow map
			if (context.SurfacePipeline && context.SurfaceSamplers && context.SurfaceSamplerCount > 0) {
				SDL_BindGPUFragmentSamplers(pass, 0, context.SurfaceSamplers, context.SurfaceSamplerCount);
			}

			WorldUniforms worldUniforms{
				.ViewMatrix = context.Camera->GetViewMatrix(),
				// Jittered, when a pass in this camera's chain asked for it: the
				// world is drawn through a projection nudged a fraction of a
				// pixel sideways, a different fraction every frame, so that a
				// pass blending frames together is averaging the coverage of the
				// pixel rather than the same sample over and over. It is the
				// projection on a camera with no such pass, which is nearly all
				// of them.
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

			// Reset per pass: a render pass starts with nothing bound
			SDL_GPUTexture *boundSurfaceTexture = nullptr;
			SDL_GPUBuffer *boundVertexBuffer = nullptr;
			SDL_GPUBuffer *boundIndexBuffer = nullptr;
			bool pushedBareFragment = false;

			if (instanced) {
				SDL_BindGPUVertexStorageBuffers(pass, 0, &InstanceBuffer, 1);

				uint64_t submitStart = measuring ? SDL_GetTicksNS() : 0;
				for (const Batch &batch : Batches) {
					// Per batch: everything in it shares a texture, so it
					// shares the answer to whether it has one
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

					// gl_InstanceIndex counts from here, so it indexes the
					// buffer directly
					SDL_DrawGPUIndexedPrimitives(
						pass, batch.Mesh->IndexCount, batch.Count, 0, 0, batch.First
					);
					partsDrawn += batch.Count;
				}

				if (measuring) {
					submitNanoseconds += SDL_GetTicksNS() - submitStart;
					profiler->AddZoneTime("Bucket", InstanceBucketNanoseconds, 1);
					profiler->AddZoneTime("Fill Instances", InstanceFillNanoseconds, (uint64_t)InstanceScratch.size());
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

			// What the instanced path left bound, so the loop below knows what
			// it can skip rebinding
			if (instanced) {
				boundSurfaceTexture = context.WhiteTexture;
				pushedBareFragment = true;
			}

			for (BasePart *part : instanced ? EMPTY_PARTS : *drawList) {
				uint64_t partStart = measuring ? SDL_GetTicksNS() : 0;

				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				PartUniforms uniforms{
					.ModelMatrix = part->GetModelMatrix(),
					.Color = glm::vec4((glm::vec3)part->Color, 1.0f - part->Transparency),
				};
				// Above is working out what to say, below is saying it: one is
				// arithmetic and the other is driver calls
				uint64_t partSubmit = measuring ? SDL_GetTicksNS() : 0;
				SDL_PushGPUVertexUniformData(context.Commands, 1, &uniforms, sizeof(PartUniforms));

				// Only the engine's own pipeline knows about part textures; a
				// surface shader has taken the fragment stage over instead
				if (!context.SurfacePipeline) {
					SDL_GPUTexture *surfaceTexture = context.WhiteTexture;
					if (anyPartTextures) {
						auto it = context.PartTextures->find(part);
						if (it != context.PartTextures->end() && it->second) {
							surfaceTexture = it->second;
						}
					}

					bool textured = surfaceTexture != context.WhiteTexture;

					// A bare part makes this block identical to the last bare
					// one: past the flag, the shader reads nothing
					if (textured || !pushedBareFragment) {
						// See the note in PrepareInstances
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

					// Only when it actually changed. Nearly every part shows the
					// same white texture as the one before it, and rebinding
					// the same pair for each of a few thousand parts is a
					// driver call apiece for no difference in what is drawn.
					if (surfaceTexture != boundSurfaceTexture) {
						SDL_GPUTextureSamplerBinding partBindings[2] = {
							shadowBinding,
							{.texture = surfaceTexture, .sampler = context.SurfaceTextureSampler},
						};
						SDL_BindGPUFragmentSamplers(pass, 0, partBindings, 2);
						boundSurfaceTexture = surfaceTexture;
					}
				}

				// Every part of a shape shares one primitive mesh, so this
				// binds once per shape however many parts there are
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
					// Indexed, not named; names are attached after the loop
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
				// Handed over once, rather than opened and closed per part.
				// Calls is the part count, so the chart can report a per-part
				// cost rather than only a total.
				// Whatever the per-part loop actually did. Suppressing these
				// whenever a batch ran left half the pass off its own chart.
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
				// The total as well as the split
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
		// Formatted once for the run. Per part this was two string allocations
		// apiece, inflating the very pass it was reporting on.
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
