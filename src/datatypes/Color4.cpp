#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <array>
#include <common.hpp>
#include <cstdio>
#include <lualib.h>
#include <sstream>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Color4);
	G_UD_IMPL_PROPS(
		Color4,
		G_UD_READONLY_PROP(Color4, R, float),
		G_UD_READONLY_PROP(Color4, G, float),
		G_UD_READONLY_PROP(Color4, B, float),
		G_UD_READONLY_PROP(Color4, A, float)
	)
	G_UD_IMPL_METHODS(
		Color4,
		G_UD_METHOD(Color4, Lerp),
		G_UD_METHOD(Color4, ToColor3),
		G_UD_METHOD(Color4, ToHSV),
		G_UD_METHOD(Color4, ToHex),
		{"__tostring", {Color4::LTostring}},
		{"__add", {Color4::LAdd}},
		{"__sub", {Color4::LSub}},
		{"__mul", {Color4::LMul}},
		{"__div", {Color4::LDiv}},
		{"__eq", {Color4::LEq}}
	)

	Color4::Color4() : R(0.0f), G(0.0f), B(0.0f), A(1.0f) {};

	Color4::Color4(float r, float g, float b, float a)
		: R(glm::clamp(r, 0.0f, 1.0f)), G(glm::clamp(g, 0.0f, 1.0f)), B(glm::clamp(b, 0.0f, 1.0f)),
		  A(glm::clamp(a, 0.0f, 1.0f)) {}

	Color4::Color4(const Color3 &color, float a) : Color4(color.R, color.G, color.B, a) {}

	Color4 Color4::fromRGB(float r, float g, float b, float a) {
		return Color4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
	};

	Color4 Color4::fromHSV(float h, float s, float v, float a) {
		return Color4(Color3::fromHSV(h, s, v), a);
	};

	Color4 Color4::fromHex(std::string_view hex) {
		if (!hex.empty() && hex.front() == '#') {
			hex.remove_prefix(1);
		}

		auto parseNibble = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};

		// Alpha defaults to opaque when the hex string omits it
		std::array<int, 4> channels = {0, 0, 0, 255};
		size_t channelCount = hex.size() / 2;

		if (hex.size() != 6 && hex.size() != 8) {
			return Color4();
		}

		for (size_t i = 0; i < channelCount; i++) {
			int high = parseNibble(hex[i * 2]);
			int low = parseNibble(hex[i * 2 + 1]);
			if (high < 0 || low < 0) return Color4();
			channels[i] = high * 16 + low;
		}

		return Color4::fromRGB(channels[0], channels[1], channels[2], channels[3]);
	}

	Color3 Color4::ToColor3() const {
		return Color3(R, G, B);
	}

	Color4 Color4::Lerp(const Color4 &goal, float alpha) const {
		return {
			R + (goal.R - R) * alpha,
			G + (goal.G - G) * alpha,
			B + (goal.B - B) * alpha,
			A + (goal.A - A) * alpha,
		};
	}

	std::tuple<float, float, float, float> Color4::ToHSV() const {
		auto [hue, saturation, value] = ToColor3().ToHSV();
		return {hue, saturation, value, A};
	}

	std::string Color4::ToHex() const {
		char buffer[10];
		std::snprintf(
			buffer,
			sizeof(buffer),
			"%02X%02X%02X%02X",
			(int)glm::round(R * 255.0f),
			(int)glm::round(G * 255.0f),
			(int)glm::round(B * 255.0f),
			(int)glm::round(A * 255.0f)
		);
		return std::string(buffer);
	}

	int Color4::LTostring(lua_State *L, Color4 *self) {
		std::ostringstream ss;
		ss << self->R << ", " << self->G << ", " << self->B << ", " << self->A;
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int Color4::LAdd(lua_State *L, Color4 *self) {
		Color4 other = CheckStackValue<Color4>(L, 2);
		StackValue<Color4>::Push(L, *self + other);
		return 1;
	}

	int Color4::LSub(lua_State *L, Color4 *self) {
		Color4 other = CheckStackValue<Color4>(L, 2);
		StackValue<Color4>::Push(L, *self - other);
		return 1;
	}

	int Color4::LMul(lua_State *L, Color4 *self) {
		if (StackValue<Color4>::Is(L, 2)) {
			StackValue<Color4>::Push(L, *self * StackValue<Color4>::From(L, 2));
		} else if (lua_isnumber(L, 2)) {
			StackValue<Color4>::Push(L, *self * (float)lua_tonumber(L, 2));
		} else {
			luaL_typeerror(L, 2, "Color4 or number");
			return 0;
		}
		return 1;
	}

	int Color4::LDiv(lua_State *L, Color4 *self) {
		if (StackValue<Color4>::Is(L, 2)) {
			StackValue<Color4>::Push(L, *self / StackValue<Color4>::From(L, 2));
		} else if (lua_isnumber(L, 2)) {
			StackValue<Color4>::Push(L, *self / (float)lua_tonumber(L, 2));
		} else {
			luaL_typeerror(L, 2, "Color4 or number");
			return 0;
		}
		return 1;
	}

	int Color4::LEq(lua_State *L, Color4 *self) {
		if (!StackValue<Color4>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		Color4 other = StackValue<Color4>::From(L, 2);
		lua_pushboolean(L, *self == other);
		return 1;
	}
}
