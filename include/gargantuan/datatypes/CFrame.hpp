#pragma once

#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <lua.h>
#include <tuple>

namespace gargantuan {
	// enum class RotationOrder : int { XYZ, XZY, YZX, YXZ, ZXY, ZYX };

	struct CFrame : public Userdata<CFrame> {
	  public:
		G_UD_DECL_PRELUDE(CFrame)

		static constexpr float CF_EPSILON = 1e-6;
		static constexpr glm::mat3 DEFAULT_ROTATION =
			glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
		typedef std::
			tuple<double, double, double, double, double, double, double, double, double, double, double, double>
				Components;

		glm::vec3 Position = {0, 0, 0};
		glm::mat3 Rotation = {};

		CFrame();
		CFrame(glm::vec3 position);
		CFrame(float x, float y, float z);
		CFrame(glm::vec3 position, glm::vec3 lookAtTarget);
		CFrame(glm::vec3 position, glm::mat3 rotation);
		CFrame(
			float x,
			float y,
			float z,
			float r00,
			float r01,
			float r02,
			float r10,
			float r11,
			float r12,
			float r20,
			float r21,
			float r22
		);

		// Composes the rotations as Rx * Ry * Rz
		static CFrame Angles(float x, float y, float z);
		// Composes them as Ry * Rx * Rz, the order Orientation is reported in
		static CFrame fromEulerAnglesYXZ(float x, float y, float z);
		static CFrame fromAxisAngle(glm::vec3 axis, float angle);
		static CFrame lookAt(glm::vec3 at, glm::vec3 target, glm::vec3 up = {0, 1, 0});
		// NOTE: the third axis is the back vector (Roblox names it vZ), which
		// points opposite the look vector
		static CFrame fromMatrix(glm::vec3 position, glm::vec3 right, glm::vec3 up, glm::vec3 back);
		static CFrame fromQuaternion(float x, float y, float z, float w, glm::vec3 position);

		glm::vec3 GetRightVector() const;
		glm::vec3 GetUpVector() const;
		glm::vec3 GetLookVector() const;

		CFrame Inverse() const;
		CFrame Lerp(const CFrame &goal, double alpha) const;
		CFrame Orthonormalize() const;
		// NOTE: XToY functions are supposedly tuples on Roblox, not gon do allat rn
		CFrame ToWorldSpace(const CFrame &cf) const;
		CFrame ToObjectSpace(const CFrame &cf) const;
		glm::vec3 PointToWorldSpace(const glm::vec3 &point) const;
		glm::vec3 PointToObjectSpace(const glm::vec3 &point) const;
		glm::vec3 VectorToWorldSpace(const glm::vec3 &point) const;
		glm::vec3 VectorToObjectSpace(const glm::vec3 &point) const;
		Components GetComponents() const;
		// std::tuple<double, double, double> ToEulerAngles(RotationOrder order);
		std::tuple<double, double, double> ToEulerAnglesXYZ() const;
		std::tuple<double, double, double> ToEulerAnglesYXZ() const;
		std::tuple<double, double, double> ToOrientation() const;
		std::tuple<glm::vec3, double> ToAxisAngle() const;
		bool FuzzyEq(const CFrame &other, double epsilon = 1e-5) const;
		double AngleBetween(const CFrame &other) const;
		glm::quat ToQuaternion() const;

		static int LAdd(lua_State *L, CFrame *self);
		static int LSubtract(lua_State *L, CFrame *self);
		static int LMultiply(lua_State *L, CFrame *self);
		static int LTostring(lua_State *L, CFrame *self);
		static int LEq(lua_State *L, CFrame *self);

		static glm::vec3 SafeUnit(glm::vec3 vec, glm::vec3 fallback);
		static glm::mat3 BuildLookRotation(glm::vec3 position, glm::vec3 target, glm::vec3 up = {0, 1, 0});
		static glm::mat3 MultiplyRotation(glm::mat3 lhs, glm::mat3 rhs);

		glm::vec3 operator*(const glm::vec3 &other) const {
			return Position + (Rotation * other);
		};

		CFrame operator*(const CFrame &other) const {
			glm::vec3 transformedPosition = Position + (Rotation * other.Position);
			glm::mat3 transformedRotation = Rotation * other.Rotation;
			return CFrame(transformedPosition, transformedRotation);
		};
	};

	G_UD_STACKVALUE(CFrame);
} // namespace gargantuan
