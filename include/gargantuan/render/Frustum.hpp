#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace gargantuan {
	// World-space side planes; positive distance is inside.
	struct WorldSidePlanes {
		glm::vec4 InwardPlanes[4];
	};

	// Omit ineffective near/far planes and stay depth-convention independent.
	// Side planes still reject geometry behind the eye.
	inline WorldSidePlanes ExtractSidePlanes(const glm::mat4 &viewProjection) {
		// GLM rows are the nth component of each column.
		auto row = [&](int index) {
			return glm::vec4(
				viewProjection[0][index],
				viewProjection[1][index],
				viewProjection[2][index],
				viewProjection[3][index]
			);
		};

		glm::vec4 x = row(0), y = row(1), w = row(3);
		WorldSidePlanes planes{{w + x, w - x, w + y, w - y}};

		// Normalize for world-unit radius comparisons.
		for (auto &plane : planes.InwardPlanes) {
			float length = glm::length(glm::vec3(plane));
			if (length > 0.0f) {
				plane /= length;
			}
		}
		return planes;
	}

	// Sphere test is specialized because every part uses it; casters use capsules.
	inline bool SphereInside(const WorldSidePlanes &planes, glm::vec3 centre, float radius) {
		// Plain floats avoid four per-part temporary/call pairs.
		for (const auto &plane : planes.InwardPlanes) {
			float distance = plane.x * centre.x + plane.y * centre.y + plane.z * centre.z + plane.w;
			if (distance < -radius) {
				return false;
			}
		}
		return true;
	}

	inline int ClassifyBoxAgainstPlanes(const WorldSidePlanes &planes, glm::vec3 centre, float halfExtent) {
		int containment = 1;
		for (const auto &plane : planes.InwardPlanes) {
			float distance = plane.x * centre.x + plane.y * centre.y + plane.z * centre.z + plane.w;
			float projectedHalfExtent = (std::abs(plane.x) + std::abs(plane.y) + std::abs(plane.z)) * halfExtent;
			if (distance < -projectedHalfExtent) {
				return -1;
			}
			if (distance < projectedHalfExtent) {
				containment = 0;
			}
		}
		return containment;
	}

	inline bool CapsuleInside(const WorldSidePlanes &planes, glm::vec3 from, glm::vec3 to, float radius) {
		for (const auto &plane : planes.InwardPlanes) {
			// Reject only when both capsule ends lie outside one plane.
			float fromDistance = plane.x * from.x + plane.y * from.y + plane.z * from.z + plane.w;
			float toDistance = plane.x * to.x + plane.y * to.y + plane.z * to.z + plane.w;
			if (fromDistance < -radius && toDistance < -radius) {
				return false;
			}
		}
		return true;
	}
}
