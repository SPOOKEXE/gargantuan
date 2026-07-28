#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lua.h>
#include <lualib.h>
#include <sstream>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(UDim);
	G_UD_IMPL_PROPS(
		UDim,

		G_UD_READONLY_PROP(UDim, Scale, float),
		G_UD_READONLY_PROP(UDim, Offset, int)
	)
	G_UD_IMPL_METHODS(
		UDim,
		G_UD_METHOD(UDim, Lerp),
		{"__tostring", {UDim::LTostring}},
		{"__add", {UDim::LAdd}},
		{"__sub", {UDim::LSub}},
		{"__unm", {UDim::LUnm}},
		{"__eq", {UDim::LEq}}
	)

	UDim::UDim(float scale, int offset) : Scale(scale), Offset(offset) {};

	UDim UDim::Lerp(const UDim &goal, float alpha) const {
		return {
			Scale + (goal.Scale - Scale) * alpha,
			(int)glm::round(Offset + (goal.Offset - Offset) * alpha),
		};
	};

	int UDim::LTostring(lua_State *L, UDim *self) {
		std::ostringstream ss;
		ss << self->Scale << ", " << self->Offset;
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int UDim::LAdd(lua_State *L, UDim *self) {
		UDim other = CheckStackValue<UDim>(L, 2);
		StackValue<UDim>::Push(L, *self + other);
		return 1;
	}

	int UDim::LSub(lua_State *L, UDim *self) {
		UDim other = CheckStackValue<UDim>(L, 2);
		StackValue<UDim>::Push(L, *self - other);
		return 1;
	}

	int UDim::LUnm(lua_State *L, UDim *self) {
		StackValue<UDim>::Push(L, -*self);
		return 1;
	}

	int UDim::LEq(lua_State *L, UDim *self) {
		if (!StackValue<UDim>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		UDim other = StackValue<UDim>::From(L, 2);
		lua_pushboolean(L, *self == other);
		return 1;
	}

} // namespace gargantuan
