#include "gargantuan/services/Workspace.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const Workspace::ClassDefinition Workspace::DEFINITION = {
		.Name = "Workspace",
		.Superclass = "WorldRoot",
		.Constructor = ClassDefinition::WrapConstructor<Workspace>(),
		.Properties = {
			{"CurrentCamera", Property::fromSimple<&Workspace::CurrentCamera>(true, false)},
		}
	};
}
