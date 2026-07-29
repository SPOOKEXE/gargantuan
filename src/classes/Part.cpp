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
			{"Shape", Property::fromSimple<&Part::Shape>(true, true)},
		}
	};

	std::unique_ptr<GpuMesh> &Part::GetMesh() const {
		// The keys are built once for the run rather than once per call. This
		// sits on the hot path of every pass -- the opaque pass asks for it for
		// every part of every frame, and the shadow pass asks again -- and
		// building the string here was a concatenation, a heap allocation and a
		// thirty character hash apiece. At a few thousand parts that was the
		// single largest thing the renderer did.
		static const std::vector<std::string> KEYS = [] {
			std::vector<std::string> keys;
			for (auto value : magic_enum::enum_values<Enums::PartType>()) {
				keys.push_back("gargantuan://meshes/" + std::string(magic_enum::enum_name(value)));
			}
			return keys;
		}();
		static const std::string FALLBACK = "gargantuan://meshes/Block";

		auto index = magic_enum::enum_index(Shape);
		return MeshProvider::GetGpuMesh(index && *index < KEYS.size() ? KEYS[*index] : FALLBACK);
	};
}
