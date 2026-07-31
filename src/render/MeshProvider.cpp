#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/render/PrimitiveMeshes.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <iterator>
#include <memory>

namespace gargantuan::MeshProvider {
	namespace {
		std::unordered_map<std::string, Mesh> UnloadedMeshes = {
			{"gargantuan://meshes/Ball", PrimitiveMeshes::Block()},
			{"gargantuan://meshes/Block", PrimitiveMeshes::Block()},
			{"gargantuan://meshes/Cylinder", PrimitiveMeshes::Block()},
			{"gargantuan://meshes/Wedge", PrimitiveMeshes::Wedge()},
			{"gargantuan://meshes/CornerWedge", PrimitiveMeshes::Block()},
		};

		std::unordered_map<std::string, std::unique_ptr<GpuMesh>> GpuMeshes;

		// Indexed by Enums::PartType. Resolved once, because the map's values
		// are node-based and their addresses are stable for the process.
		std::array<std::unique_ptr<GpuMesh> *, PrimitiveMeshCount> PrimitiveSlots{};

		const char *PRIMITIVE_KEYS[] = {
			"gargantuan://meshes/Ball",
			"gargantuan://meshes/Block",
			"gargantuan://meshes/Cylinder",
			"gargantuan://meshes/Wedge",
			"gargantuan://meshes/CornerWedge",
		};
	} // namespace

	std::unique_ptr<GpuMesh> &GetPrimitiveMesh(uint8_t meshId) {
		static std::unique_ptr<GpuMesh> missing;
		if (meshId >= std::size(PRIMITIVE_KEYS)) return missing;

		std::unique_ptr<GpuMesh> *&slot = PrimitiveSlots[meshId];
		if (!slot) slot = &GpuMeshes[PRIMITIVE_KEYS[meshId]];
		return *slot;
	}

	std::unique_ptr<GpuMesh> &GetGpuMesh(std::string id) {
		return GpuMeshes[id];
	}

	void Destroy(SDL_GPUDevice *gpu) {
		for (auto &[meshId, gpuMesh] : GpuMeshes) {
			gpuMesh->Destroy(gpu);
		}
		GpuMeshes.clear();
	}

	void UploadToGpu(SDL_GPUDevice *gpu) {
		if (UnloadedMeshes.empty()) {
			return;
		}

		auto cmd = SDL_AcquireGPUCommandBuffer(gpu);
		auto copyPass = SDL_BeginGPUCopyPass(cmd);

		for (auto &[meshId, unloadedMesh] : UnloadedMeshes) {
			// if (auto &gpuMesh = GpuMeshes.find(meshId)) {
			//     gpuMesh->Destroy(Gpu);
			// };

			auto gpuMesh = std::make_unique<GpuMesh>(unloadedMesh);
			gpuMesh->Upload(gpu, copyPass);
			GpuMeshes[meshId] = std::move(gpuMesh);
		}

		SDL_EndGPUCopyPass(copyPass);
		SDL_SubmitGPUCommandBuffer(cmd);
		UnloadedMeshes.clear();
	}
} // namespace gargantuan::MeshProvider
