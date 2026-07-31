#include "gargantuan/reflection/Enums.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/UserdataTag.hpp"

#include <SDL3/SDL_log.h>
#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <string_view>

namespace gargantuan {
	int Enums_index(lua_State *L) {
		std::string_view key = CheckStackValue<std::string_view>(L, 2);

		auto &enums = Enums::GetEnums();
		if (auto it = enums.find(key); it != enums.end()) {
			StackValue<Enum::Pointer>::Push(L, it->second);
			return 1;
		} else {
			luaL_error(L, "%s is not a valid member of \"Enum\"", key.data());
			return 0;
		}
	};

	int Enums_namecall(lua_State *L) {
		const char *key = lua_namecallatom(L, nullptr);
		if (!key) {
			luaL_error(L, "Missing method name");
			return 0;
		}

		if (std::strcmp(key, "GetEnums") == 0) {
			auto &enums = Enums::GetEnums();
			lua_createtable(L, enums.size(), 0);

			int tblIndex = 0;
			for (auto e : enums) {
				tblIndex += 1;
				StackValue<Enum::Pointer>::Push(L, e.second);
				lua_rawseti(L, -2, tblIndex);
			}

			return 1;
		}

		luaL_error(L, "Unknown method named %s", key);
		return 0;
	};

	int Enums_tostring(lua_State *L) {
		lua_pushliteral(L, "Enums");
		return 1;
	};

	int OpenLibEnum(lua_State *L) {
		lua_createtable(L, 0, 4);

		lua_pushliteral(L, "Enums");
		lua_setfield(L, -2, "__type");

		lua_pushcfunction(L, Enums_index, "Enums.__index");
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, Enums_namecall, "Enums.__namecall");
		lua_setfield(L, -2, "__namecall");

		lua_pushcfunction(L, Enums_tostring, "Enums.__tostring");
		lua_setfield(L, -2, "__tostring");

		// lua_pushvalue(L, -1);
		// lua_setreadonly(L, -1, true);
		// Userdata::CreateUserdataMetatable: g->udatamt is not a GC root here
		lua_ref(L, -1);
		lua_setuserdatametatable(L, (int)UserdataTag::Enums);

		void *p = lua_newuserdatataggedwithmetatable(L, 0, (int)UserdataTag::Enums);
		lua_setglobal(L, "Enum");

		return 0;
	}
} // namespace gargantuan
