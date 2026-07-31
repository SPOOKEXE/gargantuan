#include "gargantuan/classes/Part.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL_log.h>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <vector>

namespace gargantuan {
	const Instance::ClassDefinition Part::DEFINITION = {
		.Name = "Part",
		.Superclass = "BasePart",
		.Constructor = ClassDefinition::WrapConstructor<Part>(),
		.Properties = {
			{"Shape",
			 Property::fromReadWrite<Enums::PartType>(
				 [](Instance *instance) { return instance->Cast<Part>()->GetShape(); },
				 [](Instance *instance, Enums::PartType shape) { instance->Cast<Part>()->SetShape(shape); }
			 )},
		}
	};

	namespace {
		// Namespace scope avoids per-call local-static guards on this hot path.
		const std::vector<std::string> MESH_KEYS = [] {
			std::vector<std::string> keys;
			for (auto value : magic_enum::enum_values<Enums::PartType>()) {
				keys.push_back("gargantuan://meshes/" + std::string(magic_enum::enum_name(value)));
			}
			return keys;
		}();
		const std::string MESH_FALLBACK = "gargantuan://meshes/Block";

		// Map slots survive rehash; provider generation guards teardown invalidation.
		std::vector<std::unique_ptr<GpuMesh> *> GPU_MESH_SLOTS_BY_SHAPE;
		uint64_t GPU_MESH_SLOT_CACHE_GENERATION = 0;
	}

	std::unique_ptr<GpuMesh> &Part::GetMesh() const {
		uint64_t cacheGeneration = MeshProvider::GetGpuMeshCacheGeneration();
		if (cacheGeneration != GPU_MESH_SLOT_CACHE_GENERATION) {
			GPU_MESH_SLOTS_BY_SHAPE.assign(MESH_KEYS.size(), nullptr);
			GPU_MESH_SLOT_CACHE_GENERATION = cacheGeneration;
		}

		// Shape values are contiguous from zero, so the cast is the shape index.
		size_t shapeIndex = (size_t)GetShape();
		if (shapeIndex >= MESH_KEYS.size()) {
			return MeshProvider::GetOrCreateGpuMeshSlot(MESH_FALLBACK);
		}

		std::unique_ptr<GpuMesh> *&gpuMeshSlot = GPU_MESH_SLOTS_BY_SHAPE[shapeIndex];
		if (!gpuMeshSlot) {
			gpuMeshSlot = MeshProvider::GetGpuMeshSlot(MESH_KEYS[shapeIndex]);
		}
		return *gpuMeshSlot;
	};
}
