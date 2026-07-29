#pragma once

#include <glm/glm.hpp>

namespace gargantuan {
	struct alignas(16) InstanceData {
		glm::mat4 ModelMatrix;
		glm::vec4 Color;
		glm::vec4 SurfaceNormal;
		glm::vec4 SurfaceTransform;
	};
} // namespace gargantuan
