#pragma once

#include "gargantuan/reflection/Enums.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lua.h>

namespace gargantuan {
	G_ENUM(
		Material,

		Plastic,
		SmoothPlastic,
		Neon,
		Wood,
		WoodPlanks,
		Marble,
		Slate,
		Concrete,
		Granite,
		Brick,
		Pebble,
		Cobblestone,
		Rock,
		Sandstone,
		Basalt,
		CrackedLava,
		Limestone,
		Asphalt,
		LeafyGrass,
		Grass,
		Ground,
		Mud,
		Sand,
		Snow,
		Glacier,
		Ice,
		Salt,
		Metal,
		CorrodedMetal,
		DiamondPlate,
		Foil,
		Pavement,
		Glass,
		ForceField,
		Fabric,
		Cardboard,
		Carpet,
		Plaster,
		Air,
		Water
	)

	struct PhysicalProperties : public Userdata<PhysicalProperties> {
	  public:
		G_UD_DECL_PRELUDE(PhysicalProperties)

		float Density = 0.7f;
		float Friction = 0.3f;
		float Elasticity = 0.5f;
		float FrictionWeight = 1.0f;
		float ElasticityWeight = 1.0f;

		PhysicalProperties() = default;
		PhysicalProperties(float density, float friction, float elasticity);
		PhysicalProperties(
			float density, float friction, float elasticity, float frictionWeight, float elasticityWeight
		);
		explicit PhysicalProperties(Enums::Material material);

		static int LTostring(lua_State *L, PhysicalProperties *self);
		static int LEq(lua_State *L, PhysicalProperties *self);

		bool operator==(const PhysicalProperties &other) const {
			return Density == other.Density && Friction == other.Friction && Elasticity == other.Elasticity &&
				   FrictionWeight == other.FrictionWeight && ElasticityWeight == other.ElasticityWeight;
		};
	};

	G_UD_STACKVALUE(PhysicalProperties);
}
