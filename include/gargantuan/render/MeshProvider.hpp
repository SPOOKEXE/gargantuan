#pragma once

#include "gargantuan/render/GpuMesh.hpp"

#include <string>
#include <memory>

namespace gargantuan::MeshProvider {
	std::unique_ptr<GpuMesh> &GetOrCreateGpuMeshSlot(const std::string &id);
	std::unique_ptr<GpuMesh> *GetGpuMeshSlot(const std::string &id);
	extern uint64_t GpuMeshCacheGeneration;
	inline uint64_t GetGpuMeshCacheGeneration() {
		return GpuMeshCacheGeneration;
	}
	void UploadToGpu(SDL_GPUDevice *gpu);
	void DestroyAllGpuMeshes(SDL_GPUDevice *gpu);
};
