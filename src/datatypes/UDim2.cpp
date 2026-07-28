#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lua.h>
#include <lualib.h>
#include <sstream>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(UDim2);
	G_UD_IMPL_PROPS(
		UDim2,

		G_UD_READONLY_PROP(UDim2, X, UDim),
		G_UD_READONLY_PROP(UDim2, Y, UDim),
		// Roblox aliases X/Y as Width/Height
		{"Width", {[](lua_State *L, auto *inst) -> int { return G_UD_READONLY_PROP_IMPL(UDim2, X, UDim)(L, inst); },
				   nullptr, G_UD_REFLECT_TYPE(UDim)}},
		{"Height", {[](lua_State *L, auto *inst) -> int { return G_UD_READONLY_PROP_IMPL(UDim2, Y, UDim)(L, inst); },
					nullptr, G_UD_REFLECT_TYPE(UDim)}}
	)
	G_UD_IMPL_METHODS(
		UDim2,
		G_UD_METHOD(UDim2, Lerp),
		{"__tostring", {UDim2::LTostring}},
		{"__add", {UDim2::LAdd}},
		{"__sub", {UDim2::LSub}},
		{"__mul", {UDim2::LMul}},
		{"__div", {UDim2::LDiv}},
		{"__unm", {UDim2::LUnm}},
		{"__eq", {UDim2::LEq}}
	)

	UDim2::UDim2(UDim x, UDim y) : X(x), Y(y) {};

	UDim2::UDim2(float xScale, int xOffset, float yScale, int yOffset)
		: X(xScale, xOffset), Y(yScale, yOffset) {};

	UDim2 UDim2::fromScale(float x, float y) {
		return {{x, 0}, {y, 0}};
	};

	UDim2 UDim2::fromOffset(int x, int y) {
		return {{0.0f, x}, {0.0f, y}};
	};

	UDim2 UDim2::Lerp(const UDim2 &goal, float alpha) const {
		return {X.Lerp(goal.X, alpha), Y.Lerp(goal.Y, alpha)};
	};

	int UDim2::LTostring(lua_State *L, UDim2 *self) {
		std::ostringstream ss;
		ss << "{" << self->X.Scale << ", " << self->X.Offset << "}, {" << self->Y.Scale << ", " << self->Y.Offset
		   << "}";
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int UDim2::LAdd(lua_State *L, UDim2 *self) {
		UDim2 other = CheckStackValue<UDim2>(L, 2);
		StackValue<UDim2>::Push(L, *self + other);
		return 1;
	}

	int UDim2::LSub(lua_State *L, UDim2 *self) {
		UDim2 other = CheckStackValue<UDim2>(L, 2);
		StackValue<UDim2>::Push(L, *self - other);
		return 1;
	}

	int UDim2::LMul(lua_State *L, UDim2 *self) {
		// Luau hands __mul its operands in source order, so the number may be on
		// either side -- find it rather than assuming index 2.
		int scalarIdx = lua_isnumber(L, 2) ? 2 : 1;
		if (!lua_isnumber(L, scalarIdx)) {
			luaL_typeerror(L, 2, "number");
			return 0;
		}

		StackValue<UDim2>::Push(L, *self * (float)lua_tonumber(L, scalarIdx));
		return 1;
	}

	int UDim2::LDiv(lua_State *L, UDim2 *self) {
		float scalar = luaL_checknumber(L, 2);
		StackValue<UDim2>::Push(L, *self / scalar);
		return 1;
	}

	int UDim2::LUnm(lua_State *L, UDim2 *self) {
		StackValue<UDim2>::Push(L, -*self);
		return 1;
	}

	int UDim2::LEq(lua_State *L, UDim2 *self) {
		if (!StackValue<UDim2>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		UDim2 other = StackValue<UDim2>::From(L, 2);
		lua_pushboolean(L, *self == other);
		return 1;
	}
} // namespace gargantuan
