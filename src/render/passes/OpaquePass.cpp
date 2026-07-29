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
		// Iterating nothing, for the frame where the batched path already drew
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

		// Per-instance data, laid out to match the std430 block the instanced
		// vertex stage reads
		struct alignas(16) InstanceData {
			glm::mat4 ModelMatrix;
			glm::vec4 Color;
			// Carried per instance so a part showing a picture can be batched
			// too. Which face the picture lands on depends on how the part is
			// turned, so it is genuinely per part; whether there is a picture
			// at all is per batch and pushed as a uniform.
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
		// Held between frames rather than built each time, so a hundred
		// thousand instances is one allocation for the run
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
		// Where each batch's next instance goes, kept so it is not allocated
		// afresh every frame
		std::vector<uint32_t> Cursors;
		// Visible parts that cannot be batched, drawn the old way after the
		// batches have gone in
		std::vector<BasePart *> Individual;
		// The three things building the instance buffer actually does, timed at
		// their own boundaries rather than per part. Phases of a loop can be
		// measured for nothing; the inside of one cannot.
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

		// Grown in powers of two and kept, so a steady scene stops reallocating
		// after its first frame
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

		// Buckets the visible parts by mesh, writes them into one buffer and
		// uploads it. False when there is nothing to do or the buffer could not
		// be had, in which case the caller falls back to drawing a part at a
		// time.
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
			// Grown, never shrunk, and never cleared. Every element written this
			// frame is written before it is read, so zeroing them first is a
			// nine megabyte memset at a hundred thousand parts for values that
			// are about to be overwritten.
			if (InstanceScratch.size() < parts.size()) {
				InstanceScratch.resize(parts.size());
			}

			// A handful of meshes, so a linear scan finds the bucket faster
			// than hashing would. Parts of a shape end up contiguous, which is
			// what lets each shape be one draw.
			// Which bucket each part landed in, remembered rather than worked
			// out twice. The second pass would otherwise ask every part for its
			// mesh again and scan the buckets again to find where it goes.
			PartBatch.clear();
			PartBatch.reserve(parts.size());
			Individual.clear();
			// The upload only ever reads the first `running` of them, so a
			// larger vector left over from a busier frame costs nothing


			for (BasePart *part : parts) {
				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					PartBatch.push_back(SKIPPED);
					continue;
				}

				// A part showing a picture batches with the others that show the
				// same one. It used to be drawn on its own, because the face
				// the picture lands on differs per part and that lived in a
				// uniform; it rides in the instance buffer now.
				// Two pointer tests before the map. The map holds only the parts
				// that show something, but it was being asked about every part
				// of every camera -- a hash of a pointer to answer "no" for the
				// great majority, when the part is carrying the answer itself.
				SDL_GPUTexture *texture = context.WhiteTexture;
				if (anyPartTextures && (part->SurfaceCamera || part->SurfaceImage)) {
					auto found = context.PartTextures->find(part);
					if (found != context.PartTextures->end() && found->second) {
						texture = found->second;
					}
				}

				uint32_t bucket = SKIPPED;
				for (uint32_t candidate = 0; candidate < (uint32_t)Batches.size(); candidate++) {
					if (Batches[candidate].Mesh == mesh.get() && Batches[candidate].Texture == texture) {
						bucket = candidate;
						break;
					}
				}
				if (bucket == SKIPPED) {
					auto *primitive = part->Cast<Part>();
					bucket = (uint32_t)Batches.size();
					Batches.push_back(
						{mesh.get(), texture, primitive ? primitive->Shape : Enums::PartType::Block, 0, 0}
					);
				}

				PartBatch.push_back(bucket);
				Batches[bucket].Count++;
			}

			InstanceBucketNanoseconds = SDL_GetTicksNS() - started;
			if (Batches.empty()) {
				return false;
			}

			uint64_t fillStarted = SDL_GetTicksNS();

			// Where each shape's run starts, so the parts can be written
			// straight into their own stretch on one pass over the list
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
			for (size_t index = 0; index < parts.size(); index++) {
				uint32_t bucket = PartBatch[index];
				if (bucket == SKIPPED) {
					continue;
				}

				BasePart *part = parts[index];
				InstanceData &instance = InstanceScratch[Cursors[bucket]++];

				// The model matrix written straight into its slot, in plain
				// floats. GetModelMatrix returns a mat4 by value and builds it
				// out of glm temporaries, all of which are function calls until
				// something inlines them; this is the same arithmetic with none
				// of the ceremony, and it runs once per visible part per frame.
				//
				//   T * R * S  =  [ r0*sx  r1*sy  r2*sz  position ]
				const glm::mat3 &rotation = part->CFrame.Rotation;
				const glm::vec3 &size = part->Size;
				const glm::vec3 &position = part->CFrame.Position;
				float *model = &instance.ModelMatrix[0][0];

				model[0] = rotation[0][0] * size.x;
				model[1] = rotation[0][1] * size.x;
				model[2] = rotation[0][2] * size.x;
				model[3] = 0.0f;
				model[4] = rotation[1][0] * size.y;
				model[5] = rotation[1][1] * size.y;
				model[6] = rotation[1][2] * size.y;
				model[7] = 0.0f;
				model[8] = rotation[2][0] * size.z;
				model[9] = rotation[2][1] * size.z;
				model[10] = rotation[2][2] * size.z;
				model[11] = 0.0f;
				model[12] = position.x;
				model[13] = position.y;
				model[14] = position.z;
				model[15] = 1.0f;

				glm::vec3 colour = (glm::vec3)part->Color;
				instance.Color.x = colour.x;
				instance.Color.y = colour.y;
				instance.Color.z = colour.z;
				instance.Color.w = 1.0f - part->Transparency;

				// Only for the parts that actually show something. The shader
				// reads these two behind a flag that is off for the rest, and
				// working them out for every part meant a matrix multiply and a
				// normalise apiece for the great majority that never look.
				if (Batches[bucket].Texture == White) {
					continue;
				}

				// Carried through the same transform the vertex stage puts the
				// mesh's own normals through, rather than the correct inverse
				// transpose. Wrong the same way on both sides, so the two still
				// line up, which is all a match needs.
				glm::vec4 match = part->GetSurfaceMatch();
				float nx = match.x, ny = match.y, nz = match.z;
				if (nx * nx + ny * ny + nz * nz > 0.0f) {
					float wx = rotation[0][0] * nx + rotation[1][0] * ny + rotation[2][0] * nz;
					float wy = rotation[0][1] * nx + rotation[1][1] * ny + rotation[2][1] * nz;
					float wz = rotation[0][2] * nx + rotation[1][2] * ny + rotation[2][2] * nz;
					float length = std::sqrt(wx * wx + wy * wy + wz * wz);
					if (length > 0.0f) {
						nx = wx / length;
						ny = wy / length;
						nz = wz / length;
					}
				}

				instance.SurfaceNormal = glm::vec4(nx, ny, nz, match.w);
				instance.SurfaceTransform = glm::vec4(
					part->SurfaceTiling.GetX(),
					part->SurfaceTiling.GetY(),
					part->SurfaceOffset.GetX(),
					part->SurfaceOffset.GetY()
				);
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
			// Cycled, so this frame's upload does not wait on the GPU still
			// reading last frame's copy
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
			// Read once. Two clock reads a part is affordable; asking the
			// profiler whether it is switched on, for every part of every
			// frame, is not something a measurement should be doing to the
			// thing it is measuring.
			Profiler *profiler = Profiler::GetCurrent();
			const bool measuring = profiler && profiler->IsEnabled();
			uint64_t transformNanoseconds = 0;
			uint64_t submitNanoseconds = 0;
			uint64_t partsDrawn = 0;
			// The ones that went through one at a time, counted apart from the
			// batched ones so the chart can price them separately
			uint64_t individualParts = 0;

			// Tallied here and handed over once at the end. Calling the
			// profiler per part meant a linear scan of the counter list with a
			// string compare at every step, twice, for every part -- a couple
			// of million string comparisons a frame at this part count, all of
			// it inside the region being timed.
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeDraws{};
			std::array<uint64_t, magic_enum::enum_count<Enums::PartType>()> shapeTriangles{};

			// Everything the pass needs to decide before it opens, because the
			// instance upload is a copy pass and a copy pass cannot run inside
			// a render pass.
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

			// One draw per shape rather than one per part. Only when nothing
			// needs saying about a part on its own: a surface shader has taken
			// the fragment stage over, or something in the world is showing a
			// picture and wants its own uniforms. Those keep the per-part path,
			// which is the one every other example has always used.
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

			// What is currently bound, so the loop below can tell a binding that
			// would change something from one that would not. Reset per pass
			// rather than kept, because a render pass starts with nothing bound.
			SDL_GPUTexture *boundSurfaceTexture = nullptr;
			SDL_GPUBuffer *boundVertexBuffer = nullptr;
			SDL_GPUBuffer *boundIndexBuffer = nullptr;
			// Whether the fragment uniforms currently hold the "no picture on
			// this part" values, which every bare part would otherwise push again
			bool pushedBareFragment = false;

			if (instanced) {
				// The whole visible set in one buffer, and a draw per shape.
				// Everything below this is the per-part path, which is what
				// runs when a scene has pictures on its surfaces.
				SDL_BindGPUVertexStorageBuffers(pass, 0, &InstanceBuffer, 1);

				uint64_t submitStart = measuring ? SDL_GetTicksNS() : 0;
				for (const Batch &batch : Batches) {
					// Per batch, not per part: everything in it shares a
					// texture, so it shares the answer to whether it has one
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
					// buffer directly and each shape reads its own run of it
					SDL_DrawGPUIndexedPrimitives(
						pass, batch.Mesh->IndexCount, batch.Count, 0, 0, batch.First
					);
					partsDrawn += batch.Count;
				}

				if (measuring) {
					submitNanoseconds += SDL_GetTicksNS() - submitStart;
					// The three phases in their own right rather than one lump
					// called Transforms, which is what the per-part path calls
					// a different thing
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

			if (instanced && !Individual.empty()) {
				// Back to the pipeline that takes one object at a time, and the
				// world uniforms again because the slot belongs to whichever
				// pipeline is bound
				SDL_BindGPUGraphicsPipeline(pass, Pipeline);
				SDL_PushGPUVertexUniformData(context.Commands, 0, &worldUniforms, sizeof(WorldUniforms));
			}

			// What the instanced path left bound, so the loop below knows what
			// it can skip rebinding
			if (instanced) {
				boundSurfaceTexture = context.WhiteTexture;
				pushedBareFragment = true;
			}

			for (BasePart *part : instanced ? Individual : *drawList) {
				uint64_t partStart = measuring ? SDL_GetTicksNS() : 0;

				auto &mesh = part->GetMesh();
				if (!mesh || !mesh->VertexBuffer || !mesh->IndexBuffer) {
					continue;
				}

				PartUniforms uniforms{
					.ModelMatrix = part->GetModelMatrix(),
					.Color = glm::vec4((glm::vec3)part->Color, 1.0f - part->Transparency),
				};
				// Everything above is this pass working out what to say;
				// everything below is saying it. The two answer different
				// questions -- one is arithmetic and the other is driver calls
				// -- and which of them is larger decides what is worth doing
				// about it.
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

					// A part with no picture on it makes this block of uniforms
					// identical to the last one that also had none: the shader
					// reads nothing but the flag once the flag is zero. Pushing
					// it again for each of thousands of bare parts is a driver
					// call apiece to say the same thing.
					if (textured || !pushedBareFragment) {
						// Carried through the same transform the vertex stage
						// puts the mesh's own normals through, rather than the
						// correct inverse transpose. Wrong the same way on both
						// sides, so the two still line up, which is all a match
						// needs.
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

				// Same again for the geometry. Every part of a given shape
				// shares one primitive mesh, so a scene of blocks and balls
				// binds two pairs of buffers however many parts it has -- as
				// long as it only rebinds when the mesh changes.
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

				// Counted by shape rather than timed by shape. Timing one draw
				// would measure how long it took to write a command into a
				// buffer, which has almost nothing to do with what a cylinder
				// costs; how many of them went in, and how many triangles they
				// carried, is the part the CPU here actually decides.
				if (measuring) {
					// Indexed, not named. The names are attached once the loop
					// is over.
					auto *primitive = part->Cast<Part>();
					auto index = magic_enum::enum_index(primitive ? primitive->Shape : Enums::PartType::Block);
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
				// Whatever the per-part loop actually did, which in a scene that
				// mixes the two paths is the textured minority rather than
				// nothing. Suppressing these whenever a batch ran left the
				// larger half of this pass off its own chart.
				if (individualParts > 0) {
					profiler->AddZoneTime("Individual Transforms", transformNanoseconds, individualParts);
					profiler->AddZoneTime("Individual Submit", submitNanoseconds, individualParts);
				}
			}
			if (measuring) {
				// One call per shape rather than two per part
				for (size_t index = 0; index < shapeDraws.size(); index++) {
					if (shapeDraws[index] == 0) {
						continue;
					}
					const auto &names = CounterNames(magic_enum::enum_values<Enums::PartType>()[index]);
					profiler->Add(names.first, shapeDraws[index]);
					profiler->Add(names.second, shapeTriangles[index]);
				}
				// The total as well as the split. Adding up eight rows in your
				// head to find out whether the frame got heavier is not what a
				// readout is for, and the balls alone are usually most of it.
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
		// The two counter names for a shape, formatted once for the run.
		//
		// Building them per part per frame is what this replaced, and it was
		// not free: two string allocations for every part drawn, which at a few
		// thousand parts was a measurable slice of this pass -- reported by the
		// profiler, as part of the pass it was inflating.
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
