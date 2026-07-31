#pragma once

#include <glm/glm.hpp>

namespace gargantuan {
	struct alignas(16) PartInstance {
		glm::vec4 ModelRows[3];
		glm::vec4 Color;
		glm::vec4 SurfaceNormalAndRule;
		glm::vec4 SurfaceTilingOffset;
	};
}
