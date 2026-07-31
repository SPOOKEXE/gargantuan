#pragma once

#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	struct UDim2 : public Userdata<UDim2> {
	  public:
		G_UD_DECL_PRELUDE(UDim2);

		UDim X = {};
		UDim Y = {};

		UDim2() = default;
		UDim2(UDim x, UDim y);
		UDim2(float xScale, int xOffset, float yScale, int yOffset);

		static UDim2 fromScale(float x, float y);
		static UDim2 fromOffset(int x, int y);

		UDim2 Lerp(const UDim2 &goal, float alpha) const;

		static int LTostring(lua_State *L, UDim2 *self);
		static int LAdd(lua_State *L, UDim2 *self);
		static int LSub(lua_State *L, UDim2 *self);
		static int LMul(lua_State *L, UDim2 *self);
		static int LDiv(lua_State *L, UDim2 *self);
		static int LUnm(lua_State *L, UDim2 *self);
		static int LEq(lua_State *L, UDim2 *self);

		UDim2 operator+(const UDim2 &other) const {
			return {X + other.X, Y + other.Y};
		};
		UDim2 operator-(const UDim2 &other) const {
			return {X - other.X, Y - other.Y};
		};
		UDim2 operator-() const {
			return {-X, -Y};
		};
		UDim2 operator*(float scalar) const {
			return {
				{X.Scale * scalar, (int)glm::round(X.Offset * scalar)},
				{Y.Scale * scalar, (int)glm::round(Y.Offset * scalar)},
			};
		};
		UDim2 operator/(float scalar) const {
			return *this * (1.0f / scalar);
		};
		bool operator==(const UDim2 &other) const {
			return X == other.X && Y == other.Y;
		};
	};

	G_UD_STACKVALUE(UDim2);
}
