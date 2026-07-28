#pragma once

#include "gargantuan/scripting/Userdata.hpp"

#include <glm/glm.hpp>
#include <lua.h>
#include <string>
#include <tuple>

namespace gargantuan {
	struct Color3 : public Userdata<Color3> {
	  public:
		G_UD_DECL_PRELUDE(Color3)

		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;

		Color3();
		Color3(float r, float g, float b);

		static Color3 fromRGB(float r, float g, float b);
		static Color3 fromHSV(float h, float s, float v);
		static Color3 fromHex(std::string_view hex);

		Color3 Lerp(const Color3 &goal, float alpha) const;
		std::tuple<float, float, float> ToHSV() const;
		std::string ToHex() const;

		static int LTostring(lua_State *L, Color3 *self);
		static int LAdd(lua_State *L, Color3 *self);
		static int LSub(lua_State *L, Color3 *self);
		static int LMul(lua_State *L, Color3 *self);
		static int LDiv(lua_State *L, Color3 *self);
		static int LEq(lua_State *L, Color3 *self);

		Color3 operator+(const Color3 &other) const {
			return {R + other.R, G + other.G, B + other.B};
		};
		Color3 operator-(const Color3 &other) const {
			return {R - other.R, G - other.G, B - other.B};
		};
		Color3 operator*(const Color3 &other) const {
			return {R * other.R, G * other.G, B * other.B};
		};
		Color3 operator*(float scalar) const {
			return {R * scalar, G * scalar, B * scalar};
		};
		Color3 operator/(const Color3 &other) const {
			return {R / other.R, G / other.G, B / other.B};
		};
		Color3 operator/(float scalar) const {
			return {R / scalar, G / scalar, B / scalar};
		};
		bool operator==(const Color3 &other) const {
			return R == other.R && G == other.G && B == other.B;
		};

		operator glm::vec3() const {
			return {R, G, B};
		}
	};

	G_UD_STACKVALUE(Color3);
} // namespace gargantuan
