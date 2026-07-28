#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/scripting/StackValue.hpp"

#include <lualib.h>

namespace gargantuan {
	int LibCFrame_new(lua_State *L) {
		int argumentCount = lua_gettop(L);

		if (argumentCount == 0) {
			StackValue<CFrame>::Push(L, CFrame());
			return 1;
		} else if (argumentCount == 1) {
			glm::vec3 pos = CheckStackValue<glm::vec3>(L, 1);
			StackValue<CFrame>::Push(L, CFrame(pos));
			return 1;
		} else if (argumentCount == 2) {
			glm::vec3 pos = CheckStackValue<glm::vec3>(L, 1);
			glm::vec3 target = CheckStackValue<glm::vec3>(L, 2);

			StackValue<CFrame>::Push(L, CFrame(pos, target));
			return 1;
		} else if (argumentCount == 3) {
			float x = luaL_checknumber(L, 1);
			float y = luaL_checknumber(L, 2);
			float z = luaL_checknumber(L, 3);
			StackValue<CFrame>::Push(L, CFrame(x, y, z));
			return 1;
		} else if (argumentCount == 7) {
			// position followed by a quaternion
			glm::vec3 position = {
				(float)luaL_checknumber(L, 1),
				(float)luaL_checknumber(L, 2),
				(float)luaL_checknumber(L, 3),
			};
			StackValue<CFrame>::Push(
				L,
				CFrame::fromQuaternion(
					luaL_checknumber(L, 4),
					luaL_checknumber(L, 5),
					luaL_checknumber(L, 6),
					luaL_checknumber(L, 7),
					position
				)
			);
			return 1;
		} else if (argumentCount == 12) {
			StackValue<CFrame>::Push(
				L,
				CFrame(
					luaL_checknumber(L, 1),
					luaL_checknumber(L, 2),
					luaL_checknumber(L, 3),
					luaL_checknumber(L, 4),
					luaL_checknumber(L, 5),
					luaL_checknumber(L, 6),
					luaL_checknumber(L, 7),
					luaL_checknumber(L, 8),
					luaL_checknumber(L, 9),
					luaL_checknumber(L, 10),
					luaL_checknumber(L, 11),
					luaL_checknumber(L, 12)
				)
			);
			return 1;
		}

		luaL_error(L, "unsupported constructor");
		return 0;
	}

	int LibCFrame_Angles(lua_State *L) {
		float rx = luaL_checknumber(L, 1);
		float ry = luaL_checknumber(L, 2);
		float rz = luaL_checknumber(L, 3);
		StackValue<CFrame>::Push(L, CFrame::Angles(rx, ry, rz));
		return 1;
	}

	int LibCFrame_fromEulerAnglesYXZ(lua_State *L) {
		float rx = luaL_checknumber(L, 1);
		float ry = luaL_checknumber(L, 2);
		float rz = luaL_checknumber(L, 3);
		StackValue<CFrame>::Push(L, CFrame::fromEulerAnglesYXZ(rx, ry, rz));
		return 1;
	}

	int LibCFrame_fromAxisAngle(lua_State *L) {
		auto axis = CheckStackValue<glm::vec3>(L, 1);
		float angle = luaL_checknumber(L, 2);
		StackValue<CFrame>::Push(L, CFrame::fromAxisAngle(axis, angle));
		return 1;
	}

	int LibCFrame_fromMatrix(lua_State *L) {
		auto position = CheckStackValue<glm::vec3>(L, 1);
		auto right = CheckStackValue<glm::vec3>(L, 2);
		auto up = CheckStackValue<glm::vec3>(L, 3);
		// Roblox derives the third axis when it is omitted
		auto back = lua_isnoneornil(L, 4) ? glm::cross(right, up) : CheckStackValue<glm::vec3>(L, 4);
		StackValue<CFrame>::Push(L, CFrame::fromMatrix(position, right, up, back));
		return 1;
	}

	int LibCFrame_lookAt(lua_State *L) {
		auto at = CheckStackValue<glm::vec3>(L, 1);
		auto target = CheckStackValue<glm::vec3>(L, 2);
		auto up = lua_isnoneornil(L, 3) ? glm::vec3(0, 1, 0) : CheckStackValue<glm::vec3>(L, 3);
		StackValue<CFrame>::Push(L, CFrame::lookAt(at, target, up));
		return 1;
	}

	luaL_Reg LibCFrame[] = {
		{"new", LibCFrame_new},
		{"Angles", LibCFrame_Angles},
		// Roblox spells the same XYZ constructor three ways
		{"fromEulerAngles", LibCFrame_Angles},
		{"fromEulerAnglesXYZ", LibCFrame_Angles},
		{"fromEulerAnglesYXZ", LibCFrame_fromEulerAnglesYXZ},
		{"fromOrientation", LibCFrame_fromEulerAnglesYXZ},
		{"fromAxisAngle", LibCFrame_fromAxisAngle},
		{"fromMatrix", LibCFrame_fromMatrix},
		{"lookAt", LibCFrame_lookAt},
		{nullptr, nullptr},
	};

	int OpenLibCFrame(lua_State *L) {
		luaL_register(L, "CFrame", LibCFrame);

		// luaL_register leaves the library table on the stack
		StackValue<CFrame>::Push(L, CFrame());
		lua_setfield(L, -2, "identity");

		lua_pop(L, 1);
		return 0;
	}
} // namespace gargantuan
