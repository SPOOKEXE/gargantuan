#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/render/Shader.hpp"

#include <SDL3/SDL.h>
#include <magic_enum/magic_enum.hpp>
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

		FileShader Shader{
			.VertexFilepath = GetShaderPath("opaque.vert"),
			.VertexUniformBufferCount = 2,
			.FragmentFilepath = GetShaderPath("opaque.frag"),
			.FragmentUniformBufferCount = 2,
			.FragmentSamplerCount = 2,
		};

		OpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
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
			SDL_BindGPUGraphicsPipeline(pass, context.SurfacePipeline ? context.SurfacePipeline : Pipeline);

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

			// What the frustum walk already worked out, rather than the world
			// filtered again a part at a time. Without a walk there is nothing
			// to iterate but everything, which is wasteful and never wrong.
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

			// Nothing to look up when nothing in the world has a picture on it,
			// which spares a hash lookup for every part of every frame in the
			// scenes that never use one
			const bool anyPartTextures = context.PartTextures && !context.PartTextures->empty();

			for (BasePart *part : *drawList) {
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
				CountPrimitive(part, mesh->IndexCount);

				if (measuring) {
					uint64_t partEnd = SDL_GetTicksNS();
					transformNanoseconds += partSubmit - partStart;
					submitNanoseconds += partEnd - partSubmit;
				}
				partsDrawn++;
			}

			if (measuring) {
				// Handed over once, rather than opened and closed per part.
				// Calls is the part count, so the chart can report a per-part
				// cost rather than only a total.
				profiler->AddZoneTime("Transforms", transformNanoseconds, partsDrawn);
				profiler->AddZoneTime("Submit", submitNanoseconds, partsDrawn);
			}
			if (profiler) {
				profiler->Add("Parts Drawn", partsDrawn);
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

		// Non-const, because Instance::Cast has a const overload set that is
		// ambiguous on its own and only resolves from a mutable pointer
		static void CountPrimitive(BasePart *part, uint32_t indexCount) {
			Profiler *profiler = Profiler::GetCurrent();
			// Asked here rather than left to Add, so that a place with the
			// panel closed pays nothing at all for counters nobody is reading
			if (!profiler || !profiler->IsEnabled()) {
				return;
			}

			// Everything of one shape lands on one pair of counters, which is
			// the whole point: three hundred blocks are interesting as three
			// hundred blocks and not as three hundred rows
			auto *primitive = part->Cast<Part>();
			const auto &names = CounterNames(primitive ? primitive->Shape : Enums::PartType::Block);

			profiler->Add(primitive ? names.first : "Other Draws", 1);
			profiler->Add(primitive ? names.second : "Other Tris", indexCount / 3);
		}
	};

	std::unique_ptr<RenderPass> CreateOpaquePass(SDL_GPUDevice *gpu, SDL_GPUTextureFormat swapchainFormat) {
		return std::make_unique<OpaquePass>(gpu, swapchainFormat);
	}
} // namespace gargantuan
