#include "gargantuan/scripting/ScriptEngine.hpp"
#include <lua.h>

namespace gargantuan {
	int OpenLibBase(lua_State *L) {
		lua_createtable(L, 0, 0);
		{
			lua_pushliteral(L, "gargantuan");
			lua_setfield(L, -2, "name");

			lua_pushliteral(L, "https://gargantuan.teamfireworks.org/");
			lua_setfield(L, -2, "url");

			lua_createtable(L, 0, 0);
			{
				lua_pushliteral(L, "0.0.0-indev");
				lua_setfield(L, -2, "display");

				lua_createtable(L, 0, 0);
				{
					lua_pushliteral(L, "https://github.com/teamfireworks/gargantuan.git/");
					lua_setfield(L, -2, "url");
				}
				lua_pushvalue(L, -1);
				lua_setreadonly(L, -1, true);
				lua_setfield(L, -2, "git");
			}
			lua_pushvalue(L, -1);
			lua_setreadonly(L, -1, true);
			lua_setfield(L, -2, "version");
		}
		lua_pushvalue(L, -1);
		lua_setreadonly(L, -1, true);
		lua_setglobal(L, "_RUNTIME");

		return 0;
	}
} // namespace gargantuan
