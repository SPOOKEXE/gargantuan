#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lualib.h>
#include <sstream>
#include <unordered_map>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(PhysicalProperties);
	G_UD_IMPL_PROPS(
		PhysicalProperties,
		G_UD_READONLY_PROP(PhysicalProperties, Density, float),
		G_UD_READONLY_PROP(PhysicalProperties, Friction, float),
		G_UD_READONLY_PROP(PhysicalProperties, Elasticity, float),
		G_UD_READONLY_PROP(PhysicalProperties, FrictionWeight, float),
		G_UD_READONLY_PROP(PhysicalProperties, ElasticityWeight, float)
	)
	G_UD_IMPL_METHODS(
		PhysicalProperties,
		{"__tostring", {PhysicalProperties::LTostring}},
		{"__eq", {PhysicalProperties::LEq}}
	)

	// Density, Friction, Elasticity, FrictionWeight, ElasticityWeight
	struct MaterialProperties {
		float Density;
		float Friction;
		float Elasticity;
		float FrictionWeight;
		float ElasticityWeight;
	};

	static const std::unordered_map<Enums::Material, MaterialProperties> MATERIAL_PROPERTIES = {
		{Enums::Material::Plastic, {0.70f, 0.30f, 0.50f, 1.0f, 1.0f}},
		{Enums::Material::SmoothPlastic, {0.70f, 0.20f, 0.50f, 1.0f, 1.0f}},
		{Enums::Material::Neon, {0.70f, 0.30f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::Wood, {0.35f, 0.48f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::WoodPlanks, {0.35f, 0.48f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::Marble, {2.56f, 0.20f, 0.17f, 1.0f, 1.0f}},
		{Enums::Material::Slate, {2.69f, 0.40f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::Concrete, {2.40f, 0.70f, 0.20f, 0.3f, 1.0f}},
		{Enums::Material::Granite, {2.69f, 0.40f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::Brick, {1.92f, 0.80f, 0.15f, 0.3f, 1.0f}},
		{Enums::Material::Pebble, {2.40f, 0.40f, 0.17f, 1.0f, 1.5f}},
		{Enums::Material::Cobblestone, {2.69f, 0.50f, 0.17f, 1.0f, 1.0f}},
		{Enums::Material::Rock, {2.69f, 0.50f, 0.17f, 1.0f, 1.0f}},
		{Enums::Material::Sandstone, {2.69f, 0.50f, 0.15f, 5.0f, 1.0f}},
		{Enums::Material::Basalt, {2.69f, 0.70f, 0.15f, 0.3f, 1.0f}},
		{Enums::Material::CrackedLava, {2.69f, 0.65f, 0.15f, 1.0f, 1.0f}},
		{Enums::Material::Limestone, {2.69f, 0.50f, 0.15f, 1.0f, 1.0f}},
		{Enums::Material::Asphalt, {2.36f, 0.80f, 0.20f, 0.3f, 1.0f}},
		{Enums::Material::LeafyGrass, {0.90f, 0.40f, 0.20f, 2.0f, 1.0f}},
		{Enums::Material::Grass, {0.90f, 0.40f, 0.10f, 2.0f, 1.0f}},
		{Enums::Material::Ground, {0.90f, 0.45f, 0.10f, 1.0f, 1.0f}},
		{Enums::Material::Mud, {0.90f, 0.30f, 0.07f, 3.0f, 1.0f}},
		{Enums::Material::Sand, {1.60f, 0.50f, 0.05f, 5.0f, 2.5f}},
		{Enums::Material::Snow, {0.90f, 0.30f, 0.03f, 3.0f, 4.0f}},
		{Enums::Material::Glacier, {0.92f, 0.05f, 0.15f, 3.0f, 1.0f}},
		{Enums::Material::Ice, {0.92f, 0.02f, 0.15f, 3.0f, 1.0f}},
		{Enums::Material::Salt, {2.16f, 0.50f, 0.05f, 1.0f, 1.0f}},
		{Enums::Material::Metal, {7.85f, 0.40f, 0.25f, 1.0f, 1.0f}},
		{Enums::Material::CorrodedMetal, {7.85f, 0.70f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::DiamondPlate, {7.85f, 0.35f, 0.25f, 1.0f, 1.0f}},
		{Enums::Material::Foil, {2.70f, 0.40f, 0.25f, 1.0f, 1.0f}},
		{Enums::Material::Pavement, {2.69f, 0.50f, 0.17f, 0.3f, 1.0f}},
		{Enums::Material::Glass, {2.40f, 0.25f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::ForceField, {2.40f, 0.25f, 0.20f, 1.0f, 1.0f}},
		{Enums::Material::Fabric, {0.70f, 0.35f, 0.05f, 1.0f, 1.0f}},
		{Enums::Material::Cardboard, {0.70f, 0.50f, 0.05f, 1.0f, 2.0f}},
		{Enums::Material::Carpet, {1.10f, 0.40f, 0.25f, 1.0f, 2.0f}},
		{Enums::Material::Plaster, {0.75f, 0.35f, 0.20f, 1.0f, 2.0f}},
		{Enums::Material::Air, {0.01f, 0.01f, 0.01f, 1.0f, 1.0f}},
		{Enums::Material::Water, {1.00f, 0.00f, 0.01f, 1.0f, 1.0f}},
	};

	PhysicalProperties::PhysicalProperties(float density, float friction, float elasticity)
		: Density(density), Friction(friction), Elasticity(elasticity) {}

	PhysicalProperties::PhysicalProperties(
		float density, float friction, float elasticity, float frictionWeight, float elasticityWeight
	)
		: Density(density), Friction(friction), Elasticity(elasticity), FrictionWeight(frictionWeight),
		  ElasticityWeight(elasticityWeight) {}

	PhysicalProperties::PhysicalProperties(Enums::Material material) {
		auto it = MATERIAL_PROPERTIES.find(material);
		if (it == MATERIAL_PROPERTIES.end()) {
			return;
		}

		const MaterialProperties &properties = it->second;
		Density = properties.Density;
		Friction = properties.Friction;
		Elasticity = properties.Elasticity;
		FrictionWeight = properties.FrictionWeight;
		ElasticityWeight = properties.ElasticityWeight;
	}

	int PhysicalProperties::LTostring(lua_State *L, PhysicalProperties *self) {
		std::ostringstream ss;
		ss << self->Density << ", " << self->Friction << ", " << self->Elasticity << ", " << self->FrictionWeight
		   << ", " << self->ElasticityWeight;
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int PhysicalProperties::LEq(lua_State *L, PhysicalProperties *self) {
		if (!StackValue<PhysicalProperties>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		PhysicalProperties other = StackValue<PhysicalProperties>::From(L, 2);
		lua_pushboolean(L, *self == other);
		return 1;
	}
}
