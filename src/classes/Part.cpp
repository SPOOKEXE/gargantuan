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
		// At namespace scope, not inside GetMesh. A function-local static is
		// guarded: every call checks whether it has been initialised yet, and
		// this is called for every part of every pass of every frame. Three of
		// them meant three guard checks before any work started.
		const std::vector<std::string> MESH_KEYS = [] {
			std::vector<std::string> keys;
			for (auto value : magic_enum::enum_values<Enums::PartType>()) {
				keys.push_back("gargantuan://meshes/" + std::string(magic_enum::enum_name(value)));
			}
			return keys;
		}();
		const std::string MESH_FALLBACK = "gargantuan://meshes/Block";

		// The slot each shape lives in, resolved once and then held. The map
		// keeps its elements put across a rehash, so the only thing that can
		// invalidate these is the provider being torn down -- which is what the
		// generation is for.
		std::vector<std::unique_ptr<GpuMesh> *> MESH_SLOTS;
		uint64_t MESH_SLOT_GENERATION = 0;
	} // namespace

	std::unique_ptr<GpuMesh> &Part::GetMesh() const {
		uint64_t generation = MeshProvider::GetGeneration();
		if (generation != MESH_SLOT_GENERATION) {
			MESH_SLOTS.assign(MESH_KEYS.size(), nullptr);
			MESH_SLOT_GENERATION = generation;
		}

		// A plain cast rather than magic_enum::enum_index, which searches. The
		// enum is declared with no explicit values, so its items are 0..n-1 and
		// the cast is the index.
		size_t index = (size_t)GetShape();
		if (index >= MESH_KEYS.size()) {
			return MeshProvider::GetGpuMesh(MESH_FALLBACK);
		}

		std::unique_ptr<GpuMesh> *&slot = MESH_SLOTS[index];
		if (!slot) {
			slot = MeshProvider::GetGpuMeshSlot(MESH_KEYS[index]);
		}
		return *slot;
	};
}
