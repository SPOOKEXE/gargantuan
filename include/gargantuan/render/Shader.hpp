#pragma once

#include <SDL3/SDL.h>
#include <filesystem>

namespace gargantuan {
	std::filesystem::path GetShaderPath(const std::filesystem::path &relativePath);

	void GetSupportedShaderBinaryFormat(
		SDL_GPUDevice *gpu, SDL_GPUShaderFormat &format, std::string &bytecodeExtension, std::string &entrypoint
	);

	struct ShaderProgram {
	  public:
		SDL_GPUShader *VertexShader = nullptr;
		SDL_GPUShader *FragmentShader = nullptr;

		void Destroy(SDL_GPUDevice *gpu);
	};

	struct FileShader final : public ShaderProgram {
	  public:
		std::filesystem::path VertexFilepathStem;
		Uint32 VertexUniformBufferCount = 1;
		Uint32 VertexSamplerCount = 0;
		Uint32 VertexStorageBufferCount = 0;

		std::filesystem::path FragmentFilepathStem;
		Uint32 FragmentUniformBufferCount = 0;
		Uint32 FragmentSamplerCount = 0;

		void Init(SDL_GPUDevice *gpu);

	  private:
		SDL_GPUShader *CreateShaderFromFile(SDL_GPUDevice *gpu, std::filesystem::path path, SDL_GPUShaderCreateInfo info);
	};
}
