#include "gargantuan/classes/Part.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL_log.h>
#include <magic_enum/magic_enum.hpp>
#include <memory>

namespace gargantuan {
	const Instance::ClassDefinition Part::DEFINITION = {
		.Name = "Part",
		.Superclass = "BasePart",
		.Constructor = ClassDefinition::WrapConstructor<Part>(),
		.Properties = {
			{
				// Shape is the mesh handle, so changing it has to rebuild the
				// part's render row.
				"Shape",
				Property::fromReadWrite<Enums::PartType>(
					[](Instance *self) { return self->Cast<Part>()->Shape; },
					[](Instance *self, Enums::PartType value) {
						auto *part = self->Cast<Part>();
						part->Shape = value;
						part->MarkChanged(ecs::ChangeFlags::Visual);
					}
				),
			},
		}
	};

	// Shape is the mesh handle. Mesh data is shared -- ten thousand blocks
	// reference one cube -- so the part stores a one-byte id into the interned
	// table rather than anything mesh-shaped of its own.
	std::unique_ptr<GpuMesh> &Part::GetMesh() const {
		return MeshProvider::GetPrimitiveMesh((uint8_t)Shape);
	};
}
