// Recording one camera: the scene passes first, then its chain of surface,
// post-process, and compute shaders, each one reading what the last wrote.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/render/RenderPass.hpp"
#include "gargantuan/render/ShaderPresets.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <vector>

namespace gargantuan {
	// Everything a pass reads that belongs to the scene rather than to the
	// target being drawn into. Both draw paths need all of it, and a field one
	// path forgot is a picture that is quietly wrong rather than a crash.
	void RenderProvider::BindSceneToFrame(FrameContext &frameContext, const DrawContext &drawContext) {
		frameContext.WorldRoot = drawContext.WorldRoot;
		frameContext.Camera = drawContext.Camera;
		frameContext.LightDirection = glm::normalize(drawContext.LightDirection);
		frameContext.ShadowMapTexture = ShadowMapTexture;
		frameContext.ShadowSampler = ShadowSampler;

		EnsureWhiteTextureAndSamplers();
		ResolvePartTextures(drawContext.WorldRoot);
		SceneDrawIndex.EnsureDrawKeys(drawContext.WorldRoot);

		frameContext.PartTextures = &PartTextures;
		frameContext.InstancesBySlot = &SceneDrawIndex.DrawInstances;
		frameContext.DirtyInstanceSlots = &SceneDrawIndex.DirtyDrawSlots;
		frameContext.InstancesAllDirty = SceneDrawIndex.DrawInstancesAllDirty;
		frameContext.MeshTextureBatchKeys = &SceneDrawIndex.DrawKeys;
		frameContext.SurfaceTextures = &SurfaceTexturesBySlot;
		frameContext.SurfaceSlotsComplete = AllSurfacesGotSlots;
		frameContext.WhiteTexture = WhiteTexture;
		frameContext.SurfaceTextureSampler = PartSurfaceSampler ? PartSurfaceSampler : ShadowSampler;

		// Every path needs one, including the ones that bypass PlanRedraw.
		Camera *camera = drawContext.Camera.get();
		frameContext.VisibleParts = &SceneDrawIndex.EnsureVisibleSet(
			camera, drawContext.WorldRoot, drawContext.LightDirection, ComputeCameraSignature(camera)
		);
	}

	void RenderProvider::RecordShaderChain(
		SDL_GPUCommandBuffer *commands,
		Camera *camera,
		CameraTextureSet &target,
		size_t firstShaderIndex,
		bool shouldSnapshotChainPrefix
	) {
		G_PROFILE("Shader Chain");
		if (!camera || !target.ChainPingPongTexture) {
			return;
		}

		std::vector<std::shared_ptr<ShaderScript>> chain = BuildShaderChain(camera);
		if (chain.empty() || firstShaderIndex >= chain.size()) {
			return;
		}

		size_t cacheCutIndex = FindFirstAlwaysRedrawShaderIndex(chain);

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
			.Time = glm::vec4((float)Scene.TimeSeconds, 0.0f, 0.0f, 0.0f),
			.Jitter = glm::vec4(camera->Jitter, camera->PreviousJitter),
		};

		// readTexture always names the latest ping-pong output.
		SDL_GPUTexture *readTexture = target.ColorTexture;
		SDL_GPUTexture *writeTexture = target.ChainPingPongTexture;

		for (size_t index = firstShaderIndex; index < chain.size(); index++) {
			// Snapshot immediately before the always-redraw tail.
			if (index == cacheCutIndex && shouldSnapshotChainPrefix && target.CachedChainPrefixTexture) {
				SDL_GPUBlitInfo snapshot{
					.source = {.texture = readTexture, .w = target.Width, .h = target.Height},
					.destination = {.texture = target.CachedChainPrefixTexture, .w = target.Width, .h = target.Height},
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.filter = SDL_GPU_FILTER_NEAREST,
				};
				SDL_BlitGPUTexture(commands, &snapshot);
			}

			auto &shader = chain[index];
			// Skip scripts with neither an asset name nor compiled bytecode.
			if (!shader || (shader->SourceAssetName.empty() && !shader->HasBytecode())) {
				continue;
			}

			if (auto *post = shader->Cast<PostProcessShader>()) {
				CompiledShader *compiled = GetPostProcessShader(post);
				if (!compiled) {
					continue;
				}

				auto parameters = PackParameters(post, *compiled);
				// SDL rejects zero-length uniform pushes.
				if (parameters.empty()) {
					parameters.resize(sizeof(glm::vec4), 0);
				}
				uint32_t parameterBytes = (uint32_t)parameters.size();

				SDL_GPUColorTargetInfo colorTarget{
					.texture = writeTexture,
					.load_op = SDL_GPU_LOADOP_DONT_CARE,
					.store_op = SDL_GPU_STOREOP_STORE,
				};
				// Slot 0 is camera output; script images follow set order.
				SDL_GPUTextureSamplerBinding bindings[1 + ShaderProperties::MAXIMUM_IMAGES];
				bindings[0] = {.texture = readTexture, .sampler = ShaderSampler};
				uint32_t bindingCount = 1;

				for (auto &textureSource : post->GetProperties()->GetTextureSources()) {
					SDL_GPUTexture *texture = ResolveTextureSource(camera, textureSource);
					if (!texture || bindingCount > ShaderProperties::MAXIMUM_IMAGES) {
						continue;
					}
					bindings[bindingCount++] = {.texture = texture, .sampler = GetTextureSourceSampler(textureSource)};
				}

				// Validate sampler count before the driver does.
				uint32_t declaredSamplerCount =
					compiled->ResourceCounts.WasSpirvParsed ? compiled->ResourceCounts.SampledImages : 1;
				if (bindingCount != declaredSamplerCount) {
					SDL_Log(
						"Shader '%s' declares %u sampler(s) but %u were supplied; give it %u image(s) with "
					"Properties:SetImage",
						post->SourceAssetName.empty() ? "<code>" : post->SourceAssetName.c_str(),
						declaredSamplerCount,
						bindingCount,
						declaredSamplerCount > 0 ? declaredSamplerCount - 1 : 0
					);
					continue;
				}

				SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);
				SDL_BindGPUGraphicsPipeline(pass, compiled->GraphicsPipeline);
				SDL_BindGPUFragmentSamplers(pass, 0, bindings, bindingCount);
				SDL_PushGPUFragmentUniformData(commands, 0, &builtin, sizeof(BuiltinUniforms));
				SDL_PushGPUFragmentUniformData(commands, 1, parameters.data(), parameterBytes);
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

				SDL_GPUStorageTextureReadWriteBinding writeBinding{.texture = writeTexture, .cycle = false};
				SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(commands, &writeBinding, 1, nullptr, 0);
				SDL_BindGPUComputePipeline(pass, compiled->ComputePipeline);
				SDL_BindGPUComputeStorageTextures(pass, 0, &readTexture, 1);
				SDL_PushGPUComputeUniformData(commands, 0, &builtin, sizeof(BuiltinUniforms));
				SDL_PushGPUComputeUniformData(commands, 1, parameters.data(), parameterBytes);

				// Round up edge groups; the shader discards out-of-bounds lanes.
				uint32_t groupX = (uint32_t)glm::max(compute->ThreadGroupSize.x, 1.0f);
				uint32_t groupY = (uint32_t)glm::max(compute->ThreadGroupSize.y, 1.0f);
				SDL_DispatchGPUCompute(
					pass, (target.Width + groupX - 1) / groupX, (target.Height + groupY - 1) / groupY, 1
				);
				SDL_EndGPUComputePass(pass);
			} else {
				// Bare ShaderScript has no executable stage.
				continue;
			}

			std::swap(readTexture, writeTexture);
		}

		// Normalize final output into the camera texture.
		if (readTexture != target.ColorTexture) {
			SDL_GPUBlitInfo blit{
				.source = {.texture = readTexture, .w = target.Width, .h = target.Height},
				.destination = {.texture = target.ColorTexture, .w = target.Width, .h = target.Height},
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.filter = SDL_GPU_FILTER_NEAREST,
			};
			SDL_BlitGPUTexture(commands, &blit);
		}
	}

	// Prepare surface pipeline, parameters, and post-shadow-map images.
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

		// Create sampler before constructing bindings.
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

		// Slot 0 is always the shadow map.
		samplerStorage.clear();
		samplerStorage.push_back({.texture = frameContext.ShadowMapTexture, .sampler = ShadowSampler});

		for (auto &source : camera->SurfaceShader->GetProperties()->GetTextureSources()) {
			SDL_GPUTexture *texture = ResolveTextureSource(camera, source);
			if (!texture) {
				continue;
			}
			samplerStorage.push_back({.texture = texture, .sampler = GetTextureSourceSampler(source)});
		}

		uint32_t declaredSamplerCount = surface->ResourceCounts.WasSpirvParsed ? surface->ResourceCounts.SampledImages : 1;
		if (samplerStorage.size() != declaredSamplerCount) {
			SDL_Log(
				"Surface shader '%s' declares %u sampler(s) but %zu were supplied",
				camera->SurfaceShader->SourceAssetName.empty() ? "<code>" : camera->SurfaceShader->SourceAssetName.c_str(),
				declaredSamplerCount,
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
		SDL_GPUCommandBuffer *commands, DrawContext &drawContext, const CameraTextureSet &target
	) {
		G_PROFILE("Camera Passes");
		if (!commands || !target.ColorTexture || !target.DepthStencilAttachmentTexture || !ShadowMapTexture) {
			return false;
		}

		Camera *camera = drawContext.Camera.get();
		TemporalNeeds needs = GetTemporalNeeds(camera);
		if (camera) {
			EnsureTemporalTargets(commands, camera, CameraTargets[camera], needs);
			// Advance jitter only with the world pixels it samples.
			camera->AdvanceJitter(needs.Jitter);
		}

		FrameContext frameContext;
		frameContext.Commands = commands;
		BindSceneToFrame(frameContext, drawContext);

		frameContext.ColorTargetTexture = target.ColorTexture;
		frameContext.DepthTexture = target.DepthStencilAttachmentTexture;
		frameContext.Width = target.Width;
		frameContext.Height = target.Height;
		// Velocity pass requires both declared attachments.
		bool canRecordMotionPass = needs.NeedsVelocityAndViewDepth && target.VelocityTexture && target.ViewDepthTexture;
		frameContext.VelocityTarget = canRecordMotionPass ? target.VelocityTexture : nullptr;
		frameContext.LinearViewDepthTexture = canRecordMotionPass ? target.ViewDepthTexture : nullptr;

		// SurfaceShader replaces the opaque fragment stage.
		std::vector<uint8_t> surfaceParameters;
		std::vector<SDL_GPUTextureSamplerBinding> surfaceSamplers;
		PrepareSurfaceShader(
			frameContext, drawContext.Camera.get(), OFFSCREEN_FORMAT, surfaceParameters, surfaceSamplers
		);

		// Record shadow production before opaque sampling in this buffer.
		{
			G_PROFILE("Shadow");
			SDL_EndGPURenderPass(ShadowPass->Draw(Gpu, frameContext));
		}

		// Record velocity first so SurfaceShader reads this frame's values.
		if (frameContext.VelocityTarget) {
			if (RenderPass *velocity = GetVelocityPass()) {
				G_PROFILE("Motion");
				SDL_EndGPURenderPass(velocity->Draw(Gpu, frameContext));
				IsVelocityPassUsedThisFrame = true;
			}
		}

		{
			G_PROFILE("Opaque");
			SDL_EndGPURenderPass(OffscreenOpaquePass->Draw(Gpu, frameContext));
		}

		// Store unjittered motion; jitter changes sampling, not position.
		if (camera && needs.NeedsVelocityAndViewDepth) {
			camera->PreviousViewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();
			camera->HasPreviousViewProjection = true;
		}

		return true;
	}

	void RenderProvider::SetAntialiasOverride(std::shared_ptr<ShaderScript> shader) {
		AntialiasOverride = std::move(shader);
	}

	std::shared_ptr<ShaderScript> RenderProvider::GetAntialiasShader() {
		// Override replaces but does not destroy the reusable built-in pass.
		if (AntialiasOverride) {
			return AntialiasOverride;
		}

		if (!AntialiasShader) {
			AntialiasShader = std::make_shared<PostProcessShader>();
			AntialiasShader->Name = "Antialias";
			AntialiasShader->SourceAssetName = "antialias";
			// Preserve pixels below this local-contrast threshold.
			AntialiasShader->GetProperties()->SetNumber("Threshold", 0.0625f);
		}
		return AntialiasShader;
	}

}
