#include "gargantuan/classes/PointLight.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
#define G_LIGHT_PROPERTY(propertyName, valueType, member)                                                              \
	{propertyName,                                                                                                     \
	 Property::fromReadWrite<valueType>(                                                                               \
		 [](Instance *self) -> valueType { return self->Cast<PointLight>()->member; },                                 \
		 [](Instance *self, valueType value) {                                                                         \
			 auto *light = self->Cast<PointLight>();                                                                   \
			 light->member = value;                                                                                    \
			 light->MarkChanged(ecs::ChangeFlags::Visual);                                                             \
		 }                                                                                                             \
	 )}

	const PointLight::ClassDefinition PointLight::DEFINITION = {
		.Name = "PointLight",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<PointLight>(),
		.Properties = {
			G_LIGHT_PROPERTY("Color", gargantuan::Color3, Color),
			G_LIGHT_PROPERTY("Brightness", float, Brightness),
			G_LIGHT_PROPERTY("Range", float, Range),
			G_LIGHT_PROPERTY("Enabled", bool, Enabled),
			G_LIGHT_PROPERTY("Shadows", bool, Shadows),
		}
	};

#undef G_LIGHT_PROPERTY
} // namespace gargantuan
