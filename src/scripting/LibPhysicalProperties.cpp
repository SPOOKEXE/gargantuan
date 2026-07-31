#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"

#include <lualib.h>

namespace gargantuan {
	int LibPhysicalProperties_new(lua_State *L) {
		// PhysicalProperties.new(material) reads the defaults for that material
		if (StackValue<Enums::Material>::Is(L, 1)) {
			StackValue<PhysicalProperties>::Push(L, PhysicalProperties(StackValue<Enums::Material>::From(L, 1)));
			return 1;
		}

		float density = luaL_checknumber(L, 1);
		float friction = luaL_checknumber(L, 2);
		float elasticity = luaL_checknumber(L, 3);

		// The weights are optional; supplying neither is the three-argument form
		if (lua_isnoneornil(L, 4) && lua_isnoneornil(L, 5)) {
			StackValue<PhysicalProperties>::Push(L, PhysicalProperties(density, friction, elasticity));
			return 1;
		}

		float frictionWeight = luaL_optnumber(L, 4, 1.0f);
		float elasticityWeight = luaL_optnumber(L, 5, 1.0f);
		StackValue<PhysicalProperties>::Push(
			L, PhysicalProperties(density, friction, elasticity, frictionWeight, elasticityWeight)
		);
		return 1;
	}

	luaL_Reg LibPhysicalProperties[]{
		{"new", LibPhysicalProperties_new},
		{nullptr, nullptr},
	};

	int OpenLibPhysicalProperties(lua_State *L) {
		luaL_register(L, "PhysicalProperties", LibPhysicalProperties);
		return 0;
	}
} // namespace gargantuan
