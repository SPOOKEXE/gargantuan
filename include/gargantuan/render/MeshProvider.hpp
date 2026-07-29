#pragma once

#include "gargantuan/render/GpuMesh.hpp"

#include <string>
#include <memory>

namespace gargantuan::MeshProvider {
	std::unique_ptr<GpuMesh> &GetGpuMesh(const std::string &id);
	// The slot a mesh lives in, which stays put for as long as the provider
	// does: unordered_map keeps its elements where they are across a rehash, so
	// a caller on a hot path can hold this rather than hashing a string again
	// for every part of every frame.
	std::unique_ptr<GpuMesh> *GetGpuMeshSlot(const std::string &id);
	// Bumped whenever the slots stop being valid, which is only Destroy. A
	// cache of slots compares against it rather than trusting that nothing has
	// happened since.
	//
	// Inline over the variable rather than a call: every part of every pass
	// asks, and at this optimisation level a call across a translation unit is
	// not free enough to spend on reading one integer.
	extern uint64_t Generation;
	inline uint64_t GetGeneration() {
		return Generation;
	}
	void UploadToGpu(SDL_GPUDevice *Gpu);
	void Destroy(SDL_GPUDevice *gpu);
}; // namespace gargantuan::MeshProvider
