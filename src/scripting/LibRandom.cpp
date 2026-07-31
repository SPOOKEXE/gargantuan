#include "gargantuan/datatypes/Random.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"

#include <lualib.h>

namespace gargantuan {
	int LibRandom_new(lua_State *L) {
		// Omitting the seed picks one from the clock, matching Roblox
		if (lua_isnoneornil(L, 1)) {
			StackValue<Random>::Push(L, Random());
			return 1;
		}

		StackValue<Random>::Push(L, Random((int64_t)luaL_checknumber(L, 1)));
		return 1;
	}

	luaL_Reg LibRandom[]{
		{"new", LibRandom_new},
		{nullptr, nullptr},
	};

	int OpenLibRandom(lua_State *L) {
		luaL_register(L, "Random", LibRandom);
		return 0;
	}
} // namespace gargantuan
