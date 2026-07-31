#pragma once

#include <cstdint>

namespace gargantuan::ecs {
	enum class ChangeFlags : uint8_t {
		None = 0,
		Transform = 1 << 0,
		Visual = 1 << 1,
		Collision = 1 << 2,
		Physics = 1 << 3,
		Hierarchy = 1 << 4,
		All = 0xFF,
	};

	constexpr ChangeFlags operator|(ChangeFlags a, ChangeFlags b) {
		return (ChangeFlags)((uint8_t)a | (uint8_t)b);
	}

	constexpr ChangeFlags &operator|=(ChangeFlags &a, ChangeFlags b) {
		a = a | b;
		return a;
	}

	constexpr bool Overlaps(ChangeFlags a, ChangeFlags b) {
		return ((uint8_t)a & (uint8_t)b) != 0;
	}
}
