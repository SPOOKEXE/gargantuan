#include "gargantuan/services/Workspace.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <memory>

namespace gargantuan {
	const Workspace::ClassDefinition Workspace::DEFINITION = {
		.Name = "Workspace",
		.Superclass = "WorldRoot",
		.Constructor = ClassDefinition::WrapConstructor<Workspace>(),
		.Properties = {
			G_UD_READWRITE_PROP(Workspace, Gravity, float),
			G_UD_READWRITE_PROP(Workspace, FallenPartsDestroyHeight, float),
			G_UD_READONLY_PROP(Workspace, DistributedGameTime, double),
			{
				"CurrentCamera",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<Instance::Pointer>::Push(L, instance->Cast<Workspace>()->CurrentCamera);
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						auto workspace = instance->Cast<Workspace>();
						auto camera = std::dynamic_pointer_cast<Camera>(
							StackValue<Instance::Pointer>::From(L, -1)
						);
						if (!camera) {
							luaL_error(L, "CurrentCamera must be a Camera");
							return 0;
						}

						workspace->CurrentCamera = camera;
						return 0;
					},
					G_UD_REFLECT_TYPE(Instance::Pointer),
				},
			},
		}
	};
}
