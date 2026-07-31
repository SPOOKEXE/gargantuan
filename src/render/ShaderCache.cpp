// RenderProvider's side of turning a ShaderScript into a GPU pipeline: compile,
// reflect, cache by asset-name key, and evict. Nothing here records a pass.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/render/PipelineBuilder.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/render/ShaderReflection.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace gargantuan {
	void *
	RenderProvider::LoadShaderBytes(const std::string &sourceAssetName, const char *stageExtension, size_t &outSize) {
		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string bytecodeExtension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, bytecodeExtension, entrypoint);

		auto path = GetShaderPath(sourceAssetName + stageExtension).string() + bytecodeExtension;
		void *bytecode = SDL_LoadFile(path.c_str(), &outSize);
		if (!bytecode) {
			SDL_Log("Shader '%s' not found at %s", sourceAssetName.c_str(), path.c_str());
		}
		return bytecode;
	}

	// The vertex stage a script never replaces: "opaque" behind a surface
	// shader, "fullscreen" behind a post-process one.
	SDL_GPUShader *RenderProvider::LoadBuiltinVertexShader(const char *name, uint32_t uniformBufferCount) {
		size_t size = 0;
		void *loaded = LoadShaderBytes(name, ".vert", size);
		if (!loaded) {
			return nullptr;
		}

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, extension, entrypoint);

		SDL_GPUShaderCreateInfo info{
			.code_size = size,
			.code = static_cast<const Uint8 *>(loaded),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_VERTEX,
			.num_uniform_buffers = uniformBufferCount,
		};
		SDL_GPUShader *shader = SDL_CreateGPUShader(Gpu, &info);
		SDL_free(loaded);

		if (!shader) {
			SDL_Log("Failed to create the %s vertex shader: %s", name, SDL_GetError());
		}
		return shader;
	}

	// A script's SPIR-V, whether the compiler produced it at runtime or it was
	// built beside the binary. HasBytecode is !empty, so empty means not found.
	std::vector<uint8_t> RenderProvider::LoadShaderBytecode(ShaderScript *shader, const char *stageExtension) {
		if (shader->HasBytecode()) {
			return shader->GetBytecode();
		}

		size_t size = 0;
		void *loaded = LoadShaderBytes(shader->SourceAssetName, stageExtension, size);
		if (!loaded) {
			return {};
		}

		const auto *bytes = static_cast<const uint8_t *>(loaded);
		std::vector<uint8_t> bytecode(bytes, bytes + size);
		SDL_free(loaded);
		return bytecode;
	}

	std::vector<uint8_t> RenderProvider::PackParameters(ShaderScript *shader, const CompiledShader &compiled) {
		const auto &layout = compiled.ParameterLayout;

		if (!layout.WasBlockFound) {
			// Without reflection, pack one slot per parameter in set order.
			const auto &slots = shader->GetProperties()->GetPackedParameters();
			std::vector<uint8_t> packed(slots.size() * sizeof(glm::vec4));
			if (!slots.empty()) {
				std::memcpy(packed.data(), slots.data(), packed.size());
			}
			return packed;
		}

		std::vector<uint8_t> packed(layout.SizeBytes, 0);
		for (const auto &[name, value] : shader->GetProperties()->GetParameters()) {
			const auto *member = layout.FindMember(name);
			if (!member || member->OffsetBytes >= packed.size()) {
				continue;
			}

			// Clamp writes to the reflected member size.
			uint32_t writable = std::min<uint32_t>(member->SizeBytes, (uint32_t)sizeof(glm::vec4));
			writable = std::min<uint32_t>(writable, (uint32_t)(packed.size() - member->OffsetBytes));
			std::memcpy(packed.data() + member->OffsetBytes, &value, writable);
		}
		return packed;
	}

	std::string RenderProvider::GetShaderCacheKey(ShaderScript *shader, const char *stageExtension) {
		if (shader->HasBytecode()) {
			return "code:" + std::to_string(shader->GetSerial()) + ":" + std::to_string(shader->GetRevision());
		}
		return shader->SourceAssetName + stageExtension;
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

		auto it = LastSeenRevisionByShaderSerial.find(serial);
		if (it == LastSeenRevisionByShaderSerial.end()) {
			LastSeenRevisionByShaderSerial[serial] = revision;
			return;
		}

		if (it->second == revision) {
			return;
		}

		// Revisions are monotonic; remove all entries for the old revision.
		std::string stem = "code:" + std::to_string(serial) + ":" + std::to_string(it->second);
		for (auto entry = ShaderCache.begin(); entry != ShaderCache.end();) {
			if (entry->first.rfind(stem, 0) != 0) {
				++entry;
				continue;
			}

			// Keep current-frame pipelines alive until unsubmitted buffers finish.
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
				// Never evict pipelines handed to this frame.
				if (compiled.LastUsedFrame == FrameIndex) {
					continue;
				}

				if (!oldestKey || compiled.LastUsedFrame < oldestFrame) {
					oldestKey = &key;
					oldestFrame = compiled.LastUsedFrame;
				}
			}

			// Exceed the bound temporarily when every entry is in use this frame.
			if (!oldestKey) {
				return;
			}

			ReleaseCachedShader(*oldestKey);
		}
	}

	RenderProvider::CompiledShader &RenderProvider::InsertCachedShader(
		const std::string &key, ShaderScript *shader
	) {
		DropSupersededShader(shader);

		CompiledShader &compiled = ShaderCache[key];
		compiled.LastUsedFrame = FrameIndex;

		// Insert then trim to the true ceiling; the returned current entry is safe.
		TrimShaderCache();
		return compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetSurfaceShader(
		SurfaceShader *shader, SDL_GPUTextureFormat colorFormat
	) {
		std::string key = GetShaderCacheKey(shader, ".frag") + "#surface" + std::to_string((int)colorFormat);

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->DidCompileFail ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
		compiled.DidCompileFail = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, extension, entrypoint);

		// Surface shaders replace only the fragment stage.
		if (!OpaqueVertexShader) {
			OpaqueVertexShader = LoadBuiltinVertexShader("opaque", 2);
			if (!OpaqueVertexShader) {
				return nullptr;
			}
		}

		std::vector<uint8_t> bytecode = LoadShaderBytecode(shader, ".frag");
		if (bytecode.empty()) {
			return nullptr;
		}
		const size_t size = bytecode.size();

		compiled.ResourceCounts = ShaderReflection::ReflectResources(bytecode.data(), size);
		SDL_GPUShaderCreateInfo fragmentInfo{
			.code_size = size,
			.code = bytecode.data(),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
			// Shadow map followed by script images.
			.num_samplers = compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.SampledImages : 1,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			.num_uniform_buffers = compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.UniformBuffers : 2,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(bytecode.data(), size, 1);

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

		compiled.DidCompileFail = false;
		return &compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetPostProcessShader(PostProcessShader *shader) {
		std::string key = GetShaderCacheKey(shader, ".frag");

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->DidCompileFail ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
		compiled.DidCompileFail = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, extension, entrypoint);

		if (!FullscreenVertexShader) {
			FullscreenVertexShader = LoadBuiltinVertexShader("fullscreen", 0);
			if (!FullscreenVertexShader) {
				return nullptr;
			}
		}

		// Prefer runtime bytecode; fall back to the built asset.
		std::vector<uint8_t> bytecode = LoadShaderBytecode(shader, ".frag");
		if (bytecode.empty()) {
			return nullptr;
		}
		const size_t size = bytecode.size();

		compiled.ResourceCounts = ShaderReflection::ReflectResources(bytecode.data(), size);
		uint32_t samplerCount = compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.SampledImages : 1;
		uint32_t uniformCount = compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.UniformBuffers : 2;

		SDL_GPUShaderCreateInfo fragmentInfo{
			.code_size = size,
			.code = bytecode.data(),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
			.num_samplers = samplerCount,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			.num_uniform_buffers = uniformCount,
		};
		SDL_GPUShader *fragment = SDL_CreateGPUShader(Gpu, &fragmentInfo);
		// Parameters use binding 1; engine builtins use binding 0.
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(bytecode.data(), size, 1);

		if (!fragment) {
			SDL_Log("Failed to create fragment shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		// gl_VertexIndex supplies the fullscreen triangle; disable vertex input and culling.
		compiled.GraphicsPipeline = PipelineBuilder()
										.SetVertexShader(FullscreenVertexShader)
										.SetFragmentShader(fragment)
										.SetVertexInputEnabled(false)
										.SetCullingEnabled(false)
										.SetColorEnabled(true)
										.SetColorFormat(OFFSCREEN_FORMAT)
										.SetDepthEnabled(false)
										.Build(Gpu);

		// Pipeline retains the shader module.
		SDL_ReleaseGPUShader(Gpu, fragment);

		if (!compiled.GraphicsPipeline) {
			SDL_Log("Failed to build a pipeline for shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.DidCompileFail = false;
		return &compiled;
	}

	RenderProvider::CompiledShader *RenderProvider::GetComputeShader(ComputeShader *shader) {
		std::string key = GetShaderCacheKey(shader, ".comp");

		if (CompiledShader *cached = FindCachedShader(key)) {
			return cached->DidCompileFail ? nullptr : cached;
		}

		CompiledShader &compiled = InsertCachedShader(key, shader);
		compiled.DidCompileFail = true;

		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string extension, entrypoint;
		GetSupportedShaderBinaryFormat(Gpu, format, extension, entrypoint);

		std::vector<uint8_t> bytecode = LoadShaderBytecode(shader, ".comp");
		if (bytecode.empty()) {
			return nullptr;
		}
		const size_t size = bytecode.size();

		compiled.ResourceCounts = ShaderReflection::ReflectResources(bytecode.data(), size);
		glm::vec3 threadGroupSize = shader->ThreadGroupSize;
		SDL_GPUComputePipelineCreateInfo info{
			.code_size = size,
			.code = bytecode.data(),
			.entrypoint = entrypoint.c_str(),
			.format = format,
			.num_samplers = 0,
			.num_readonly_storage_textures =
				compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.ReadOnlyStorageImages : 1,
			.num_readonly_storage_buffers = 0,
			.num_readwrite_storage_textures =
				compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.ReadWriteStorageImages : 1,
			.num_readwrite_storage_buffers = 0,
			.num_uniform_buffers = compiled.ResourceCounts.WasSpirvParsed ? compiled.ResourceCounts.UniformBuffers : 2,
			.threadcount_x = (uint32_t)glm::max(threadGroupSize.x, 1.0f),
			.threadcount_y = (uint32_t)glm::max(threadGroupSize.y, 1.0f),
			.threadcount_z = (uint32_t)glm::max(threadGroupSize.z, 1.0f),
		};
		compiled.ComputePipeline = SDL_CreateGPUComputePipeline(Gpu, &info);
		compiled.ParameterLayout = ShaderReflection::ReflectUniformBlock(bytecode.data(), size, 1);

		if (!compiled.ComputePipeline) {
			SDL_Log("Failed to create compute shader '%s': %s", key.c_str(), SDL_GetError());
			return nullptr;
		}

		compiled.DidCompileFail = false;
		return &compiled;
	}

	std::vector<std::shared_ptr<ShaderScript>> RenderProvider::BuildShaderChain(Camera *camera) {
		if (!camera) {
			return {};
		}

		// Built-in antialiasing runs after the camera chain.
		std::vector<std::shared_ptr<ShaderScript>> chain = camera->Shaders;
		if (camera->Antialiasing) {
			chain.push_back(GetAntialiasShader());
		}
		return chain;
	}

	size_t RenderProvider::FindFirstAlwaysRedrawShaderIndex(const std::vector<std::shared_ptr<ShaderScript>> &chain) {
		for (size_t index = 0; index < chain.size(); index++) {
			if (chain[index] && chain[index]->NeedsRedrawEveryFrame()) {
				return index;
			}
		}
		// No always-redraw pass: cache the whole chain.
		return chain.size();
	}

	void RenderProvider::ReleaseShaderCache() {
		for (auto &[_, compiled] : ShaderCache) {
			if (compiled.GraphicsPipeline) SDL_ReleaseGPUGraphicsPipeline(Gpu, compiled.GraphicsPipeline);
			if (compiled.ComputePipeline) SDL_ReleaseGPUComputePipeline(Gpu, compiled.ComputePipeline);
		}
		ShaderCache.clear();
		LastSeenRevisionByShaderSerial.clear();
	}
}
