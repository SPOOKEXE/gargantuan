#pragma once

#include "gargantuan/render/GpuMesh.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace gargantuan::MeshProvider {
	std::unique_ptr<GpuMesh> &GetGpuMesh(std::string id);

	// Primitives are interned by a one-byte handle. The component stores the
	// handle; the string name is never rebuilt to look a mesh up.
	inline constexpr uint8_t PrimitiveMeshCount = 8;
	std::unique_ptr<GpuMesh> &GetPrimitiveMesh(uint8_t meshId);
	void UploadToGpu(SDL_GPUDevice *Gpu);
	void Destroy(SDL_GPUDevice *gpu);
}; // namespace gargantuan::MeshProvider
