#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <array>
#include <common.hpp>
#include <cstdio>
#include <lualib.h>
#include <sstream>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Color3);
	G_UD_IMPL_PROPS(
		Color3,
		G_UD_READONLY_PROP(Color3, R, float),
		G_UD_READONLY_PROP(Color3, G, float),
		G_UD_READONLY_PROP(Color3, B, float)
	)
	G_UD_IMPL_METHODS(
		Color3,
		G_UD_METHOD(Color3, Lerp),
		G_UD_METHOD(Color3, ToHSV),
		G_UD_METHOD(Color3, ToHex),
		{"__tostring", {Color3::LTostring}},
		{"__add", {Color3::LAdd}},
		{"__sub", {Color3::LSub}},
		{"__mul", {Color3::LMul}},
		{"__div", {Color3::LDiv}},
		{"__eq", {Color3::LEq}}
	)

	Color3::Color3() : R(0.0f), G(0.0f), B(0.0f) {};

	Color3::Color3(float r, float g, float b)
		: R(glm::clamp(r, 0.0f, 1.0f)), G(glm::clamp(g, 0.0f, 1.0f)), B(glm::clamp(b, 0.0f, 1.0f)) {}

	Color3 Color3::fromRGB(float r, float g, float b) {
		return Color3(r / 255.0f, g / 255.0f, b / 255.0f);
	};

	Color3 Color3::fromHSV(float h, float s, float v) {
		h = glm::mod(h, 1.0f);
		s = glm::clamp(s, 0.0f, 1.0f);
		v = glm::clamp(v, 0.0f, 1.0f);

		auto i = glm::floor(h * 6);
		auto f = h * 6 - i;
		auto p = v * (1 - s);
		auto q = v * (1 - f * s);
		auto t = v * (1 - (1 - f) * s);

		i = glm::mod(i, 6.0f);

		return (i == 0)	  ? Color3(v, t, p)
			   : (i == 1) ? Color3(q, v, p)
			   : (i == 2) ? Color3(p, v, t)
			   : (i == 3) ? Color3(p, q, v)
			   : (i == 4) ? Color3(t, p, v)
						  : Color3(v, p, q);
	}

	// Accepts "#RRGGBB", "RRGGBB", "#RGB" and "RGB"
	Color3 Color3::fromHex(std::string_view hex) {
		if (!hex.empty() && hex.front() == '#') {
			hex.remove_prefix(1);
		}

		auto parseNibble = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};

		std::array<int, 3> channels = {0, 0, 0};

		if (hex.size() == 3) {
			for (size_t i = 0; i < 3; i++) {
				int nibble = parseNibble(hex[i]);
				if (nibble < 0) return Color3();
				// "abc" expands to "aabbcc"
				channels[i] = nibble * 16 + nibble;
			}
		} else if (hex.size() == 6) {
			for (size_t i = 0; i < 3; i++) {
				int high = parseNibble(hex[i * 2]);
				int low = parseNibble(hex[i * 2 + 1]);
				if (high < 0 || low < 0) return Color3();
				channels[i] = high * 16 + low;
			}
		} else {
			return Color3();
		}

		return Color3::fromRGB(channels[0], channels[1], channels[2]);
	}

	Color3 Color3::Lerp(const Color3 &goal, float alpha) const {
		return {
			R + (goal.R - R) * alpha,
			G + (goal.G - G) * alpha,
			B + (goal.B - B) * alpha,
		};
	}

	std::tuple<float, float, float> Color3::ToHSV() const {
		float max = glm::max(R, glm::max(G, B));
		float min = glm::min(R, glm::min(G, B));
		float delta = max - min;

		float hue = 0.0f;
		if (delta > 0.0f) {
			if (max == R) {
				hue = glm::mod((G - B) / delta, 6.0f);
			} else if (max == G) {
				hue = (B - R) / delta + 2.0f;
			} else {
				hue = (R - G) / delta + 4.0f;
			}
			hue /= 6.0f;
			if (hue < 0.0f) hue += 1.0f;
		}

		float saturation = max > 0.0f ? delta / max : 0.0f;
		return {hue, saturation, max};
	}

	std::string Color3::ToHex() const {
		char buffer[8];
		std::snprintf(
			buffer,
			sizeof(buffer),
			"%02X%02X%02X",
			(int)glm::round(R * 255.0f),
			(int)glm::round(G * 255.0f),
			(int)glm::round(B * 255.0f)
		);
		return std::string(buffer);
	}

	int Color3::LTostring(lua_State *L, Color3 *self) {
		std::ostringstream ss;
		ss << self->R << ", " << self->G << ", " << self->B;
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int Color3::LAdd(lua_State *L, Color3 *self) {
		Color3 other = CheckStackValue<Color3>(L, 2);
		StackValue<Color3>::Push(L, *self + other);
		return 1;
	}

	int Color3::LSub(lua_State *L, Color3 *self) {
		Color3 other = CheckStackValue<Color3>(L, 2);
		StackValue<Color3>::Push(L, *self - other);
		return 1;
	}

	int Color3::LMul(lua_State *L, Color3 *self) {
		if (StackValue<Color3>::Is(L, 2)) {
			StackValue<Color3>::Push(L, *self * StackValue<Color3>::From(L, 2));
		} else if (lua_isnumber(L, 2)) {
			StackValue<Color3>::Push(L, *self * (float)lua_tonumber(L, 2));
		} else {
			luaL_typeerror(L, 2, "Color3 or number");
			return 0;
		}
		return 1;
	}

	int Color3::LDiv(lua_State *L, Color3 *self) {
		if (StackValue<Color3>::Is(L, 2)) {
			StackValue<Color3>::Push(L, *self / StackValue<Color3>::From(L, 2));
		} else if (lua_isnumber(L, 2)) {
			StackValue<Color3>::Push(L, *self / (float)lua_tonumber(L, 2));
		} else {
			luaL_typeerror(L, 2, "Color3 or number");
			return 0;
		}
		return 1;
	}

	int Color3::LEq(lua_State *L, Color3 *self) {
		if (!StackValue<Color3>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		Color3 other = StackValue<Color3>::From(L, 2);
		lua_pushboolean(L, *self == other);
		return 1;
	}
} // namespace gargantuan
