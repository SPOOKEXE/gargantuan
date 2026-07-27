#include "gargantuan/datatypes/Random.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"

#include <cmath>
#include <lualib.h>

namespace gargantuan {
	int LibRandom_new(lua_State *L) {
		if (lua_isnoneornil(L, 1)) {
			StackValue<Random>::Push(L, Random());
		} else {
			// Seeds round down to the nearest integer, so 0 and 0.99 give identical generators.
			double seed = std::floor(luaL_checknumber(L, 1));
			StackValue<Random>::Push(L, Random(static_cast<std::int64_t>(seed)));
		};
		return 1;
	}

	luaL_Reg LibRandom[] = {
		{"new", LibRandom_new},
		{nullptr, nullptr},
	};

	int OpenLibRandom(lua_State *L) {
		luaL_register(L, "Random", LibRandom);
		return 0;
	}
} // namespace gargantuan
