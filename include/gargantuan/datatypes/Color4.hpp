#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <glm/glm.hpp>
#include <lua.h>
#include <string>
#include <tuple>

namespace gargantuan {
	// Roblox has no Color4; this is Color3 plus straight (non-premultiplied)
	// alpha, so that renderer-facing code has one type for RGBA.
	struct Color4 : public Userdata<Color4> {
	  public:
		G_UD_DECL_PRELUDE(Color4)

		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 1.0f;

		Color4();
		Color4(float r, float g, float b, float a = 1.0f);
		Color4(const Color3 &color, float a = 1.0f);

		static Color4 fromRGB(float r, float g, float b, float a = 255.0f);
		static Color4 fromHSV(float h, float s, float v, float a = 1.0f);
		// Accepts "#RRGGBB" and "#RRGGBBAA", with or without the leading '#'
		static Color4 fromHex(std::string_view hex);

		Color3 ToColor3() const;
		Color4 Lerp(const Color4 &goal, float alpha) const;
		std::tuple<float, float, float, float> ToHSV() const;
		std::string ToHex() const;

		static int LTostring(lua_State *L, Color4 *self);
		static int LAdd(lua_State *L, Color4 *self);
		static int LSub(lua_State *L, Color4 *self);
		static int LMul(lua_State *L, Color4 *self);
		static int LDiv(lua_State *L, Color4 *self);
		static int LEq(lua_State *L, Color4 *self);

		Color4 operator+(const Color4 &other) const {
			return {R + other.R, G + other.G, B + other.B, A + other.A};
		};
		Color4 operator-(const Color4 &other) const {
			return {R - other.R, G - other.G, B - other.B, A - other.A};
		};
		Color4 operator*(const Color4 &other) const {
			return {R * other.R, G * other.G, B * other.B, A * other.A};
		};
		Color4 operator*(float scalar) const {
			return {R * scalar, G * scalar, B * scalar, A * scalar};
		};
		Color4 operator/(const Color4 &other) const {
			return {R / other.R, G / other.G, B / other.B, A / other.A};
		};
		Color4 operator/(float scalar) const {
			return {R / scalar, G / scalar, B / scalar, A / scalar};
		};
		bool operator==(const Color4 &other) const {
			return R == other.R && G == other.G && B == other.B && A == other.A;
		};

		operator glm::vec4() const {
			return {R, G, B, A};
		}
	};

	G_UD_STACKVALUE(Color4);
} // namespace gargantuan
