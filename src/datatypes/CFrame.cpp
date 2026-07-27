#define GLM_ENABLE_EXPERIMENTAL

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <ext/quaternion_common.hpp>
#include <fwd.hpp>
#include <glm/glm.hpp>
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
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, CFrame({0, 0, 0}, self->Rotation));
				return 1;
			}},
		},
		{
			"X",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				lua_pushnumber(L, self->Position.x);
				return 1;
			}},
		},
		{
			"Y",
			Property{
				.Read = [](lua_State *L, CFrame *self) -> int {
					lua_pushnumber(L, self->Position.y);
					return 1;
				},
			},
		},
		{
			"Z",
			Property{
				.Read = [](lua_State *L, CFrame *self) -> int {
					lua_pushnumber(L, self->Position.z);
					return 1;
				},
			},
		},
		{
			"RightVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetRightVector());
				return 1;
			}},
		},
		{
			"UpVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetUpVector());
				return 1;
			}},
		},
		{
			"LookVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetLookVector());
				return 1;
			}},
		},
		{
			"XVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetRightVector());
				return 1;
			}},
		},
		{
			"YVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetUpVector());
				return 1;
			}},
		},
		{
			"ZVector",
			Property{.Read = [](lua_State *L, CFrame *self) -> int {
				StackValue<CFrame>::Push(L, self->GetLookVector());
				return 1;
			}},
		},
	);
	G_UD_IMPL_METHODS(
		CFrame,
		G_UD_METHOD(CFrame, Inverse),
		G_UD_METHOD(CFrame, Lerp),
		G_UD_METHOD(CFrame, Orthonormalize),
		G_UD_METHOD(CFrame, ToWorldSpace),
		G_UD_METHOD(CFrame, ToObjectSpace),
		// G_UD_METHOD(CFrame, PointToWorldSpace),
		// G_UD_METHOD(CFrame, PointToObjectSpace),
		// G_UD_METHOD(CFrame, VectorToWorldSpace),
		// G_UD_METHOD(CFrame, VectorToObjectSpace),
		// G_UD_METHOD(CFrame, GetComponents),
		// G_UD_METHOD(CFrame, ToEulerAngles),
		// G_UD_METHOD(CFrame, ToEulerAnglesXYZ),
		// G_UD_METHOD(CFrame, ToEulerAnglesYXZ),
		// G_UD_METHOD(CFrame, ToOrientation),
		// G_UD_METHOD(CFrame, ToAxisAngle),
		// G_UD_METHOD(CFrame, FuzzyEq),
		// G_UD_METHOD(CFrame, AngleBetween),
		// {"__add", Method{CFrame::LAdd}},
		// {"__sub", Method{CFrame::LSubtract}},
		{"__mul", Method{CFrame::LMultiply}},
		{"__tostring", Method{CFrame::LTostring}},
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
		: Position(x, y, z), Rotation(r00, r01, r02, r10, r11, r12, r20, r21, r22) {};

	glm::vec3 CFrame::GetRightVector() {
		return Rotation[0];
	}
	glm::vec3 CFrame::GetUpVector() {
		return Rotation[1];
	}
	glm::vec3 CFrame::GetLookVector() {
		return -Rotation[2];
	}

	CFrame CFrame::Angles(float x, float y, float z) {
		glm::mat4 rot4 = glm::eulerAngleXYZ(x, y, z);
		return CFrame(glm::vec3(0, 0, 0), glm::mat3(rot4));
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

		glm::vec3 right{m00, m10, m20};
		glm::vec3 up{m01, m11, m21};
		glm::vec3 look{-m02, -m12, -m22};

		return fromMatrix(position, right, up, look);
	}

	CFrame CFrame::Inverse() {
		glm::mat3 newRotation;
		for (int col = 0; col < 3; col++) {
			for (int row = 0; row < 3; row++) {
				newRotation[col][row] = Rotation[row][col];
			}
		}
		glm::vec3 newPosition = -1.0f * (newRotation * Position);

		return CFrame(newPosition, newRotation);
	};

	CFrame CFrame::Lerp(CFrame goal, double alpha) {
		glm::vec3 position{
			Position.x + (goal.Position.x - Position.x) * alpha,
			Position.y + (goal.Position.y - Position.y) * alpha,
			Position.z + (goal.Position.z - Position.z) * alpha,
		};

		auto q0 = ToQuaternion();
		auto q1 = goal.ToQuaternion();

		auto x0 = q0.x, y0 = q0.y, z0 = q0.z, w0 = q0.w;
		auto x1 = q1.x, y1 = q1.y, z1 = q1.z, w1 = q1.w;

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

	CFrame CFrame::Orthonormalize() {
		glm::vec3 x = GetRightVector();
		glm::vec3 y = GetUpVector();

		x = glm::normalize(x);
		y = glm::normalize(y - x * glm::dot(x, y));
		glm::vec3 z = glm::cross(x, y);

		return CFrame(Position.x, Position.y, Position.z, x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z);
	}

	CFrame CFrame::ToWorldSpace(CFrame cf) {
		return *this * cf;
	}

	CFrame CFrame::ToObjectSpace(CFrame cf) {
		return this->Inverse() * cf;
	}

	glm::quat CFrame::ToQuaternion() {
		auto cf = Orthonormalize();
		auto r = cf.Rotation;

		auto trace = r[0][0] + r[1][1] + r[2][2];

		float s, w, x, y, z;

		if (trace > 0) {
			s = glm::sqrt(trace + 1.0) * 2;
			w = 0.25 * s;
			x = (r[2][1] - r[1][2]) / s;
			y = (r[0][2] - r[2][0]) / s;
			z = (r[1][0] - r[0][1]) / s;
		} else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
			s = glm::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2;
			w = (r[2][1] - r[1][2]) / s;
			x = 0.25 * s;
			y = (r[0][1] + r[1][0]) / s;
			z = (r[0][2] + r[2][0]) / s;
		} else if (r[1][1] > r[2][2]) {
			s = glm::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2;
			w = (r[0][2] - r[2][0]) / s;
			x = (r[0][1] + r[1][0]) / s;
			y = 0.25 * s;
			z = (r[1][2] + r[2][1]) / s;
		} else {
			s = glm::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2;
			w = (r[1][0] - r[0][1]) / s;
			x = (r[0][2] + r[2][0]) / s;
			y = (r[1][2] + r[2][1]) / s;
			z = 0.25 * s;
		};

		return glm::quat(x, y, z, w);
	}

	int CFrame::LMultiply(lua_State *L, CFrame *self) {
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
			self->Rotation[0][0],
			self->Rotation[0][1],
			self->Rotation[0][2],
			self->Rotation[1][0],
			self->Rotation[1][1],
			self->Rotation[1][2],
			self->Rotation[2][0],
			self->Rotation[2][1],
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
