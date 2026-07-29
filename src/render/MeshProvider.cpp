#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/render/PrimitiveMeshes.hpp"

#include <SDL3/SDL.h>
#include <memory>

namespace gargantuan::MeshProvider {
	uint64_t Generation = 1;
	namespace {
		std::unordered_map<std::string, Mesh> UnloadedMeshes = {
			{"gargantuan://meshes/Ball", PrimitiveMeshes::Sphere()},
			{"gargantuan://meshes/Block", PrimitiveMeshes::Block()},
			{"gargantuan://meshes/Cylinder", PrimitiveMeshes::Cylinder()},
			{"gargantuan://meshes/Wedge", PrimitiveMeshes::Wedge()},
			{"gargantuan://meshes/CornerWedge", PrimitiveMeshes::Block()},
		};

		std::unordered_map<std::string, std::unique_ptr<GpuMesh>> GpuMeshes;

	} // namespace

	// By reference: taken by value, every caller paid for a copy of the key on
	// top of building it
	std::unique_ptr<GpuMesh> &GetGpuMesh(const std::string &id) {
		return GpuMeshes[id];
	}

	std::unique_ptr<GpuMesh> *GetGpuMeshSlot(const std::string &id) {
		return &GpuMeshes[id];
	}



	void Destroy(SDL_GPUDevice *gpu) {
		for (auto &[meshId, gpuMesh] : GpuMeshes) {
			gpuMesh->Destroy(gpu);
		}
		GpuMeshes.clear();
		// Every slot handed out is now dangling; saying so is what stops it
		Generation++;
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
