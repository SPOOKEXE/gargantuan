#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"

#include <lualib.h>

namespace gargantuan {
	int LibColor4_new(lua_State *L) {
		// Color4.new(color3, a) and Color4.new(r, g, b, a) are both accepted
		if (StackValue<Color3>::Is(L, 1)) {
			Color3 color = StackValue<Color3>::From(L, 1);
			StackValue<Color4>::Push(L, Color4(color, luaL_optnumber(L, 2, 1.0f)));
			return 1;
		}

		Color4 color = {
			(float)luaL_optnumber(L, 1, 0.0f),
			(float)luaL_optnumber(L, 2, 0.0f),
			(float)luaL_optnumber(L, 3, 0.0f),
			(float)luaL_optnumber(L, 4, 1.0f),
		};
		StackValue<Color4>::Push(L, color);
		return 1;
	}

	int LibColor4_fromRGB(lua_State *L) {
		StackValue<Color4>::Push(
			L,
			Color4::fromRGB(
				luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 255.0f)
			)
		);
		return 1;
	}

	int LibColor4_fromHSV(lua_State *L) {
		StackValue<Color4>::Push(
			L,
			Color4::fromHSV(
				luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 1.0f)
			)
		);
		return 1;
	}

	int LibColor4_fromHex(lua_State *L) {
		size_t length;
		const char *hex = luaL_checklstring(L, 1, &length);
		StackValue<Color4>::Push(L, Color4::fromHex({hex, length}));
		return 1;
	}

	int LibColor4_fromColor3(lua_State *L) {
		Color3 color = CheckStackValue<Color3>(L, 1);
		StackValue<Color4>::Push(L, Color4(color, luaL_optnumber(L, 2, 1.0f)));
		return 1;
	}

	luaL_Reg LibColor4[]{
		{"new", LibColor4_new},
		{"fromRGB", LibColor4_fromRGB},
		{"fromHSV", LibColor4_fromHSV},
		{"fromHex", LibColor4_fromHex},
		{"fromColor3", LibColor4_fromColor3},
		{nullptr, nullptr},
	};

	int OpenLibColor4(lua_State *L) {
		luaL_register(L, "Color4", LibColor4);
		return 0;
	}
} // namespace gargantuan
