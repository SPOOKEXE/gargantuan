#pragma once

#include <glm/glm.hpp>

namespace gargantuan {
	struct alignas(16) InstanceData {
		// The top three rows of T * R * S, transposed. The bottom row is always
		// (0,0,0,1), so the shader puts it back rather than the CPU storing and
		// uploading it -- four fewer stores per part and 16 fewer bytes.
		glm::vec4 ModelRows[3];
		glm::vec4 Color;
		glm::vec4 SurfaceNormal;
		glm::vec4 SurfaceTransform;
	};
} // namespace gargantuan
