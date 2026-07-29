#pragma once

#include "gargantuan/render/GpuMesh.hpp"

#include <string>
#include <memory>

namespace gargantuan::MeshProvider {
	std::unique_ptr<GpuMesh> &GetGpuMesh(const std::string &id);
	// Stable until Destroy; avoids repeated hot-path lookups.
	std::unique_ptr<GpuMesh> *GetGpuMeshSlot(const std::string &id);
	// Invalidates cached slots on Destroy. Inline because every part checks it.
	extern uint64_t Generation;
	inline uint64_t GetGeneration() {
		return Generation;
	}
	void UploadToGpu(SDL_GPUDevice *Gpu);
	void Destroy(SDL_GPUDevice *gpu);
}; // namespace gargantuan::MeshProvider
