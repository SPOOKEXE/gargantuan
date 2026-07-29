#define GLM_ENABLE_EXPERIMENTAL

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <ext/quaternion_common.hpp>
#include <fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <gtc/quaternion.hpp>
#include <lua.h>
#include <lualib.h>
#include <trigonometric.hpp>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(CFrame);
	G_UD_IMPL_PROPS(
		CFrame,
		G_UD_READONLY_PROP(CFrame, Position, glm::vec3),
		{
			"Rotation",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<CFrame>::Push(L, CFrame({0, 0, 0}, self->Rotation));
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(CFrame),
			},
		},
		{
			"X",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					lua_pushnumber(L, self->Position.x);
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(float),
			},
		},
		{
			"Y",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					lua_pushnumber(L, self->Position.y);
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(float),
			},
		},
		{
			"Z",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					lua_pushnumber(L, self->Position.z);
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(float),
			},
		},
		// NOTE: these push glm::vec3, which reaches Luau as a Vector3. Pushing
		// them through StackValue<CFrame> would silently build a CFrame from
		// the vector instead, because CFrame has an implicit vec3 constructor.
		{
			"RightVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, self->GetRightVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
		{
			"UpVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, self->GetUpVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
		{
			"LookVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, self->GetLookVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
		{
			"XVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, self->GetRightVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
		{
			"YVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, self->GetUpVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
		{
			// ZVector points backwards, opposite LookVector
			"ZVector",
			Property{
				.Read =
					[](lua_State *L, CFrame *self) -> int {
					StackValue<glm::vec3>::Push(L, -self->GetLookVector());
					return 1;
				},
				.ReflectType = G_UD_REFLECT_TYPE(glm::vec3),
			},
		},
	);
	G_UD_IMPL_METHODS(
		CFrame,
		G_UD_METHOD(CFrame, Inverse),
		G_UD_METHOD(CFrame, Lerp),
		G_UD_METHOD(CFrame, Orthonormalize),
		G_UD_METHOD(CFrame, ToWorldSpace),
		G_UD_METHOD(CFrame, ToObjectSpace),
		G_UD_METHOD(CFrame, PointToWorldSpace),
		G_UD_METHOD(CFrame, PointToObjectSpace),
		G_UD_METHOD(CFrame, VectorToWorldSpace),
		G_UD_METHOD(CFrame, VectorToObjectSpace),
		G_UD_METHOD(CFrame, GetComponents),
		G_UD_METHOD(CFrame, ToEulerAngles),
		G_UD_METHOD(CFrame, ToEulerAnglesXYZ),
		G_UD_METHOD(CFrame, ToEulerAnglesYXZ),
		G_UD_METHOD(CFrame, ToOrientation),
		G_UD_METHOD(CFrame, ToAxisAngle),
		G_UD_METHOD(CFrame, FuzzyEq),
		G_UD_METHOD(CFrame, AngleBetween),
		{"__add", Method{CFrame::LAdd}},
		{"__sub", Method{CFrame::LSub}},
		{"__mul", Method{CFrame::LMul}},
		{"__tostring", Method{CFrame::LTostring}},
		{"__eq", Method{CFrame::LEq}},
	);

	CFrame::CFrame() : Position(0.0f, 0.0f, 0.0f), Rotation(CFrame::DEFAULT_ROTATION) {};
	CFrame::CFrame(glm::vec3 position) : Position(position), Rotation(CFrame::DEFAULT_ROTATION) {};
	CFrame::CFrame(float x, float y, float z) : Position(x, y, z), Rotation(CFrame::DEFAULT_ROTATION) {};
	CFrame::CFrame(glm::vec3 position, glm::vec3 target)
		: Position(position), Rotation(BuildLookRotation(position, target)) {};
	CFrame::CFrame(glm::vec3 position, glm::mat3 rotation) : Position(position), Rotation(rotation) {};
	CFrame::CFrame(
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
	)
		// The components arrive in Roblox's row-major order, while Rotation
		// stores the right/up/back basis vectors as glm columns
		: Position(x, y, z), Rotation(r00, r10, r20, r01, r11, r21, r02, r12, r22) {};

	glm::vec3 CFrame::GetRightVector() const {
		return Rotation[0];
	}
	glm::vec3 CFrame::GetUpVector() const {
		return Rotation[1];
	}
	glm::vec3 CFrame::GetLookVector() const {
		// Rotation's third column points backwards, away from what the frame faces
		return -Rotation[2];
	}

	CFrame CFrame::Angles(float x, float y, float z) {
		glm::mat4 rot4 = glm::eulerAngleXYZ(x, y, z);
		return CFrame(glm::vec3(0, 0, 0), glm::mat3(rot4));
	}

	CFrame CFrame::fromEulerAnglesXYZ(float x, float y, float z) {
		return Angles(x, y, z);
	}

	// glm names the YXZ parameters yaw, pitch, roll, so they go in as (y, x, z)
	CFrame CFrame::fromEulerAnglesYXZ(float x, float y, float z) {
		glm::mat4 rotation = glm::eulerAngleYXZ(y, x, z);
		return CFrame(glm::vec3(0, 0, 0), glm::mat3(rotation));
	}

	// TODO: only the two orders CFrame itself composes are implemented; the
	// remaining four fall back to YXZ
	CFrame CFrame::fromEulerAngles(float x, float y, float z, Enums::RotationOrder order) {
		switch (order) {
		case Enums::RotationOrder::XYZ:
			return fromEulerAnglesXYZ(x, y, z);
		case Enums::RotationOrder::YXZ:
		default:
			return fromEulerAnglesYXZ(x, y, z);
		}
	}

	CFrame CFrame::fromOrientation(float x, float y, float z) {
		return fromEulerAnglesYXZ(x, y, z);
	}

	CFrame CFrame::fromAxisAngle(glm::vec3 axis, float angle) {
		glm::vec3 unit = SafeUnit(axis, glm::vec3(1, 0, 0));
		glm::quat rotation = glm::angleAxis(angle, unit);
		return CFrame(glm::vec3(0, 0, 0), glm::mat3_cast(rotation));
	}

	CFrame CFrame::fromRotationBetweenVectors(glm::vec3 from, glm::vec3 to) {
		glm::vec3 unitFrom = SafeUnit(from, glm::vec3(0, 0, -1));
		glm::vec3 unitTo = SafeUnit(to, glm::vec3(0, 0, -1));

		float alignment = glm::clamp(glm::dot(unitFrom, unitTo), -1.0f, 1.0f);
		if (alignment > 1.0f - CF_EPSILON) {
			return CFrame();
		}

		// Antiparallel vectors leave the rotation axis undetermined -- the cross
		// product degenerates to zero, so any perpendicular axis will do
		if (alignment < -1.0f + CF_EPSILON) {
			glm::vec3 perpendicular = glm::cross(unitFrom, glm::vec3(1, 0, 0));
			if (glm::dot(perpendicular, perpendicular) < CF_EPSILON) {
				perpendicular = glm::cross(unitFrom, glm::vec3(0, 1, 0));
			}
			return fromAxisAngle(perpendicular, glm::pi<float>());
		}

		return fromAxisAngle(glm::cross(unitFrom, unitTo), glm::acos(alignment));
	}

	CFrame CFrame::lookAt(glm::vec3 at, glm::vec3 target, glm::vec3 up) {
		return CFrame(at, BuildLookRotation(at, target, up));
	}

	CFrame CFrame::lookAlong(glm::vec3 at, glm::vec3 direction, glm::vec3 up) {
		// A zero direction gives BuildLookRotation nothing to aim at, so the
		// frame keeps the default orientation rather than degenerating
		if (glm::dot(direction, direction) < CF_EPSILON * CF_EPSILON) {
			return CFrame(at);
		}
		return lookAt(at, at + direction, up);
	}

	CFrame CFrame::fromMatrix(glm::vec3 position, glm::vec3 x, glm::vec3 y, glm::vec3 z) {
		glm::mat3 rot(x, y, z);
		return CFrame(position, rot);
	}

	CFrame CFrame::fromQuaternion(float x, float y, float z, float w, glm::vec3 position) {
		auto len = glm::sqrt(x * x + y * y + z * z + w * w);
		x = x / len, y = y / len, z = z / len, w = w / len;

		auto xx = x * x, yy = y * y, zz = z * z;
		auto xy = x * y, xz = x * z, yz = y * z;
		auto wx = w * x, wy = w * y, wz = w * z;

		auto m00 = 1 - 2 * (yy + zz);
		auto m01 = 2 * (xy - wz);
		auto m02 = 2 * (xz + wy);

		auto m10 = 2 * (xy + wz);
		auto m11 = 1 - 2 * (xx + zz);
		auto m12 = 2 * (yz - wx);

		auto m20 = 2 * (xz - wy);
		auto m21 = 2 * (yz + wx);
		auto m22 = 1 - 2 * (xx + yy);

		// fromMatrix's third axis is the back vector (Roblox's vZ), not the
		// look vector, so the column is taken as-is rather than negated
		glm::vec3 right{m00, m10, m20};
		glm::vec3 up{m01, m11, m21};
		glm::vec3 back{m02, m12, m22};

		return fromMatrix(position, right, up, back);
	}

	CFrame CFrame::Inverse() const {
		glm::mat3 newRotation;
		for (int col = 0; col < 3; col++) {
			for (int row = 0; row < 3; row++) {
				newRotation[col][row] = Rotation[row][col];
			}
		}
		glm::vec3 newPosition = -1.0f * (newRotation * Position);

		return CFrame(newPosition, newRotation);
	};

	CFrame CFrame::Lerp(const CFrame &goal, double alpha) const {
		glm::vec3 position{
			Position.x + (goal.Position.x - Position.x) * alpha,
			Position.y + (goal.Position.y - Position.y) * alpha,
			Position.z + (goal.Position.z - Position.z) * alpha,
		};

		// Read the components by name rather than decomposing: glm::qua stores
		// them in an anonymous struct inside a union, which MSVC refuses to
		// structured-bind.
		const glm::quat start = ToQuaternion();
		const glm::quat end = goal.ToQuaternion();

		const float x0 = start.x, y0 = start.y, z0 = start.z, w0 = start.w;
		float x1 = end.x, y1 = end.y, z1 = end.z, w1 = end.w;

		auto dot = x0 * x1 + y0 * y1 + z0 * z1 + w0 * w1;

		if (dot < 0) {
			dot = -dot;
			x1 = -x1, y1 = -y1, z1 = -z1, w1 = -w1;
		}

		float x, y, z, w;

		if (dot > 0.9995) {
			x = x0 + (x1 - x0) * alpha;
			y = y0 + (y1 - y0) * alpha;
			z = z0 + (z1 - z0) * alpha;
			w = w0 + (w1 - w0) * alpha;
		} else {
			auto theta0 = glm::acos(dot);
			auto sinTheta0 = glm::sin(theta0);

			auto theta = theta0 * alpha;
			auto sinTheta = glm::sin(theta);

			auto s0 = glm::cos(theta) - dot * sinTheta / sinTheta0;
			auto s1 = sinTheta / sinTheta0;

			x = x0 * s0 + x1 * s1;
			y = y0 * s0 + y1 * s1;
			z = z0 * s0 + z1 * s1;
			w = w0 * s0 + w1 * s1;
		}

		return fromQuaternion(x, y, z, w, position);
	};

	CFrame CFrame::Orthonormalize() const {
		glm::vec3 x = GetRightVector();
		glm::vec3 y = GetUpVector();

		x = glm::normalize(x);
		y = glm::normalize(y - x * glm::dot(x, y));
		glm::vec3 z = glm::cross(x, y);

		return CFrame(Position, glm::mat3(x, y, z));
	}

	CFrame CFrame::ToWorldSpace(const CFrame &cf) const {
		return *this * cf;
	}

	CFrame CFrame::ToObjectSpace(const CFrame &cf) const {
		return this->Inverse() * cf;
	}

	glm::vec3 CFrame::PointToWorldSpace(const glm::vec3 &point) const {
		return Position + (Rotation * point);
	}

	glm::vec3 CFrame::PointToObjectSpace(const glm::vec3 &point) const {
		return glm::transpose(Rotation) * (point - Position);
	}

	// Vectors ignore the translation; only the rotation applies
	glm::vec3 CFrame::VectorToWorldSpace(const glm::vec3 &vector) const {
		return Rotation * vector;
	}

	glm::vec3 CFrame::VectorToObjectSpace(const glm::vec3 &vector) const {
		return glm::transpose(Rotation) * vector;
	}

	CFrame::Components CFrame::GetComponents() const {
		return {
			Position.x,
			Position.y,
			Position.z,
			Rotation[0][0],
			Rotation[1][0],
			Rotation[2][0],
			Rotation[0][1],
			Rotation[1][1],
			Rotation[2][1],
			Rotation[0][2],
			Rotation[1][2],
			Rotation[2][2],
		};
	}

	// Decomposes R = Rx * Ry * Rz, the order CFrame.Angles composes them in
	std::tuple<double, double, double> CFrame::ToEulerAnglesXYZ() const {
		auto R = [this](int row, int column) { return (double)Rotation[column][row]; };

		double sy = glm::clamp(R(0, 2), -1.0, 1.0);
		double ry = glm::asin(sy);

		// Near a pole the X and Z rotations collapse into one; pin Z and solve X
		if (glm::abs(sy) > 1.0 - CF_EPSILON) {
			return {glm::atan(-R(1, 0), R(1, 1)), ry, 0.0};
		}

		return {glm::atan(-R(1, 2), R(2, 2)), ry, glm::atan(-R(0, 1), R(0, 0))};
	}

	// Decomposes R = Ry * Rx * Rz, which is what Roblox reports as Orientation
	std::tuple<double, double, double> CFrame::ToEulerAnglesYXZ() const {
		auto R = [this](int row, int column) { return (double)Rotation[column][row]; };

		double sx = glm::clamp(-R(1, 2), -1.0, 1.0);
		double rx = glm::asin(sx);

		if (glm::abs(sx) > 1.0 - CF_EPSILON) {
			return {rx, glm::atan(-R(2, 0), R(0, 0)), 0.0};
		}

		return {rx, glm::atan(R(0, 2), R(2, 2)), glm::atan(R(1, 0), R(1, 1))};
	}

	// TODO: only the two orders CFrame itself composes are implemented; the
	// remaining four fall back to YXZ
	std::tuple<double, double, double> CFrame::ToEulerAngles(Enums::RotationOrder order) {
		switch (order) {
		case Enums::RotationOrder::XYZ:
			return ToEulerAnglesXYZ();
		case Enums::RotationOrder::YXZ:
		default:
			return ToEulerAnglesYXZ();
		}
	}

	std::tuple<double, double, double> CFrame::ToOrientation() const {
		return ToEulerAnglesYXZ();
	}

	std::tuple<glm::vec3, double> CFrame::ToAxisAngle() const {
		glm::quat quaternion = ToQuaternion();

		// A negative real part describes the same rotation the long way round
		if (quaternion.w < 0.0f) {
			quaternion = glm::quat(-quaternion.w, -quaternion.x, -quaternion.y, -quaternion.z);
		}

		double angle = 2.0 * glm::acos(glm::clamp((double)quaternion.w, -1.0, 1.0));
		double sinHalf = glm::sqrt(glm::max(0.0, 1.0 - (double)quaternion.w * quaternion.w));

		// No rotation leaves the axis undefined, so report Roblox's default
		if (sinHalf < CF_EPSILON) {
			return {glm::vec3(1, 0, 0), 0.0};
		}

		return {
			glm::vec3(quaternion.x / sinHalf, quaternion.y / sinHalf, quaternion.z / sinHalf),
			angle,
		};
	}

	bool CFrame::FuzzyEq(const CFrame &other, double epsilon) const {
		for (int component = 0; component < 3; component++) {
			if (glm::abs(Position[component] - other.Position[component]) > epsilon) {
				return false;
			}
		}

		for (int column = 0; column < 3; column++) {
			for (int row = 0; row < 3; row++) {
				if (glm::abs(Rotation[column][row] - other.Rotation[column][row]) > epsilon) {
					return false;
				}
			}
		}

		return true;
	}

	double CFrame::AngleBetween(const CFrame &other) const {
		auto [axis, angle] = ToObjectSpace(other).ToAxisAngle();
		return angle;
	}

	glm::quat CFrame::ToQuaternion() const {
		auto cf = Orthonormalize();
		auto r = cf.Rotation;

		// R(row, column); Rotation indexes column-first, so the two are swapped
		auto R = [&r](int row, int column) { return r[column][row]; };

		auto trace = R(0, 0) + R(1, 1) + R(2, 2);

		float s, w, x, y, z;

		if (trace > 0) {
			s = glm::sqrt(trace + 1.0) * 2;
			w = 0.25 * s;
			x = (R(2, 1) - R(1, 2)) / s;
			y = (R(0, 2) - R(2, 0)) / s;
			z = (R(1, 0) - R(0, 1)) / s;
		} else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
			s = glm::sqrt(1.0 + R(0, 0) - R(1, 1) - R(2, 2)) * 2;
			w = (R(2, 1) - R(1, 2)) / s;
			x = 0.25 * s;
			y = (R(0, 1) + R(1, 0)) / s;
			z = (R(0, 2) + R(2, 0)) / s;
		} else if (R(1, 1) > R(2, 2)) {
			s = glm::sqrt(1.0 + R(1, 1) - R(0, 0) - R(2, 2)) * 2;
			w = (R(0, 2) - R(2, 0)) / s;
			x = (R(0, 1) + R(1, 0)) / s;
			y = 0.25 * s;
			z = (R(1, 2) + R(2, 1)) / s;
		} else {
			s = glm::sqrt(1.0 + R(2, 2) - R(0, 0) - R(1, 1)) * 2;
			w = (R(1, 0) - R(0, 1)) / s;
			x = (R(0, 2) + R(2, 0)) / s;
			y = (R(1, 2) + R(2, 1)) / s;
			z = 0.25 * s;
		};

		// glm stores quaternions as x,y,z,w but its four-argument constructor
		// takes them as w,x,y,z
		return glm::quat(w, x, y, z);
	}

	// CFrame + Vector3 and CFrame - Vector3 shift the position, leaving the
	// rotation untouched
	int CFrame::LAdd(lua_State *L, CFrame *self) {
		auto offset = CheckStackValue<glm::vec3>(L, 2);
		StackValue<CFrame>::Push(L, CFrame(self->Position + offset, self->Rotation));
		return 1;
	}

	int CFrame::LSub(lua_State *L, CFrame *self) {
		auto offset = CheckStackValue<glm::vec3>(L, 2);
		StackValue<CFrame>::Push(L, CFrame(self->Position - offset, self->Rotation));
		return 1;
	}

	int CFrame::LEq(lua_State *L, CFrame *self) {
		if (!StackValue<CFrame>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		CFrame other = StackValue<CFrame>::From(L, 2);
		lua_pushboolean(L, self->Position == other.Position && self->Rotation == other.Rotation);
		return 1;
	}

	int CFrame::LMul(lua_State *L, CFrame *self) {
		if (lua_isvector(L, 2)) {
			auto other = StackValue<glm::vec3>::From(L, 2);
			StackValue<glm::vec3>::Push(L, *self * other);
		} else if (StackValue<CFrame>::Is(L, 2)) {
			auto other = StackValue<CFrame>::From(L, 2);
			StackValue<CFrame>::Push(L, *self * other);
		} else {
			luaL_typeerror(L, 2, "Vector3 or CFrame");
			return 0;
		}

		return 1;
	}

	int CFrame::LTostring(lua_State *L, CFrame *self) {
		lua_pushfstringL(
			L,
			"%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
			self->Position.x,
			self->Position.y,
			self->Position.z,
			// printed row by row, the order CFrame.new's twelve-argument form takes
			self->Rotation[0][0],
			self->Rotation[1][0],
			self->Rotation[2][0],
			self->Rotation[0][1],
			self->Rotation[1][1],
			self->Rotation[2][1],
			self->Rotation[0][2],
			self->Rotation[1][2],
			self->Rotation[2][2]
		);
		return 1;
	}

	glm::vec3 CFrame::SafeUnit(glm::vec3 vec, glm::vec3 fallback) {
		auto magSq = vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
		if (magSq <= CF_EPSILON * CF_EPSILON) {
			return fallback;
		}

		auto mag = glm::sqrt(magSq);
		return vec / mag;
	}

	glm::mat3 CFrame::BuildLookRotation(glm::vec3 position, glm::vec3 target, glm::vec3 up) {
		glm::vec3 dir = target - position;
		float lenSq = glm::dot(dir, dir);
		if (lenSq < 1e-8f) {
			return DEFAULT_ROTATION;
		}

		glm::vec3 z = -glm::normalize(dir);

		if (glm::abs(glm::dot(up, z)) > 0.999f) {
			up = glm::vec3(0, 0, 1);
		}

		glm::vec3 x = glm::normalize(glm::cross(up, z));
		glm::vec3 y = glm::cross(z, x);

		return glm::mat3(x, y, z);
	}

	glm::mat3 CFrame::MultiplyRotation(glm::mat3 lhs, glm::mat3 rhs) {
		return lhs * rhs;
		// glm::mat3 result;
		// for (int col = 0; col < 3; col++) {
		//     for (int row = 0; row < 3; row++) {
		//         result[col][row] = lhs[0][row] * rhs[col][0] + lhs[1][row] * rhs[col][1] + lhs[2][row] * rhs[col][2];
		//     }
		// }
		// return result;
	}
} // namespace gargantuan
