#pragma once

#include "gargantuan/datatypes/CFrame.hpp"

#include <glm/glm.hpp>
#include <type_traits>

namespace gargantuan {
	template <typename T> struct LerpValue {
		static const T Lerp(const T &start, const T &goal, float alpha) {
			return alpha > 0.5 ? goal : start;
		};
	};

	template <typename T>
		requires std::is_floating_point_v<T>
	struct LerpValue<T> {
		static const T Lerp(const T &start, const T &goal, float alpha) {
			return start + (goal - start) * alpha;
		};
	};

	template <> struct LerpValue<glm::vec3> {
		static const glm::vec3 Lerp(const glm::vec3 &start, const glm::vec3 &goal, float alpha) {
			return start + (goal - start) * alpha;
		};
	};

	template <> struct LerpValue<CFrame> {
		static const CFrame Lerp(const CFrame &start, const CFrame &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};
}
