#pragma once

#include "gargantuan/scripting/Userdata.hpp"
#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	struct UDim : public Userdata<UDim> {
	  public:
		G_UD_DECL_PRELUDE(UDim);

		float Scale = 0.0f;
		int Offset = 0;

		UDim(float scale = 0.0f, int offset = 0);

		UDim Lerp(const UDim &goal, float alpha) const;

		static int LTostring(lua_State *L, UDim *self);
		static int LAdd(lua_State *L, UDim *self);
		static int LSub(lua_State *L, UDim *self);
		static int LUnm(lua_State *L, UDim *self);
		static int LEq(lua_State *L, UDim *self);

		UDim operator+(const UDim &other) const {
			return {Scale + other.Scale, Offset + other.Offset};
		};
		UDim operator-(const UDim &other) const {
			return {Scale - other.Scale, Offset - other.Offset};
		};
		UDim operator-() const {
			return {-Scale, -Offset};
		};
		bool operator==(const UDim &other) const {
			return Scale == other.Scale && Offset == other.Offset;
		};
	};

	G_UD_STACKVALUE(UDim);
}
