#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"

#include <lualib.h>

namespace gargantuan {
	int LibUDim2_new(lua_State *L) {
		// UDim2.new(xUDim, yUDim) and UDim2.new(xScale, xOffset, yScale, yOffset)
		// are both valid on Roblox
		if (StackValue<UDim>::Is(L, 1)) {
			UDim x = StackValue<UDim>::From(L, 1);
			UDim y = CheckStackValue<UDim>(L, 2);
			StackValue<UDim2>::Push(L, {x, y});
			return 1;
		}

		float xScale = luaL_optnumber(L, 1, 0.0f);
		int xOffset = luaL_optinteger(L, 2, 0);
		float yScale = luaL_optnumber(L, 3, 0.0f);
		int yOffset = luaL_optinteger(L, 4, 0);
		StackValue<UDim2>::Push(L, {xScale, xOffset, yScale, yOffset});
		return 1;
	}

	int LibUDim2_fromScale(lua_State *L) {
		float x = luaL_optnumber(L, 1, 0.0f);
		float y = luaL_optnumber(L, 2, 0.0f);
		StackValue<UDim2>::Push(L, UDim2::fromScale(x, y));
		return 1;
	}

	int LibUDim2_fromOffset(lua_State *L) {
		int x = luaL_optinteger(L, 1, 0);
		int y = luaL_optinteger(L, 2, 0);
		StackValue<UDim2>::Push(L, UDim2::fromOffset(x, y));
		return 1;
	}

	luaL_Reg LibUDim2[]{
		{"new", LibUDim2_new},
		{"fromScale", LibUDim2_fromScale},
		{"fromOffset", LibUDim2_fromOffset},
		{nullptr, nullptr},
	};

	int OpenLibUDim2(lua_State *L) {
		luaL_register(L, "UDim2", LibUDim2);
		return 0;
	}
} // namespace gargantuan
