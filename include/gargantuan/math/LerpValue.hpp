#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"

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

	template <> struct LerpValue<Vector2> {
		static const Vector2 Lerp(const Vector2 &start, const Vector2 &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};

	template <> struct LerpValue<Color3> {
		static const Color3 Lerp(const Color3 &start, const Color3 &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};

	template <> struct LerpValue<Color4> {
		static const Color4 Lerp(const Color4 &start, const Color4 &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};

	template <> struct LerpValue<UDim> {
		static const UDim Lerp(const UDim &start, const UDim &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};

	template <> struct LerpValue<UDim2> {
		static const UDim2 Lerp(const UDim2 &start, const UDim2 &goal, float alpha) {
			return start.Lerp(goal, alpha);
		};
	};
}
