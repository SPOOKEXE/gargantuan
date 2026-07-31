#pragma once

#include <glm/glm.hpp>

namespace gargantuan {
	// 32 bytes: two to a cache line, same shape as BroadphaseRow. A light's
	// position comes from whatever it is parented to, so the row is refreshed
	// off the change list rather than read back through the instance.
	struct LightRow {
		glm::vec3 Position{0.0f};
		float Range = 8.0f;
		glm::vec3 Color{1.0f};
		// Brightness folded in, plus an enabled bit, so the fill loop reads one
		// word instead of branching on the instance.
		float Brightness = 1.0f;
	};
} // namespace gargantuan
