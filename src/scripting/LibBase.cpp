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
				// NOTE: no lua_pushvalue before these. Duplicating the table
				// first leaves two copies on the stack, which makes the -2 the
				// setfield writes through the table itself rather than its
				// parent -- and it has just been sealed, so it throws.
				lua_setreadonly(L, -1, true);
				lua_setfield(L, -2, "git");
			}
			lua_setreadonly(L, -1, true);
			lua_setfield(L, -2, "version");
		}
		lua_setreadonly(L, -1, true);
		lua_setglobal(L, "_RUNTIME");

		return 0;
	}
} // namespace gargantuan
