#pragma once

#include <lua.h>
#include <lualib.h>

namespace gargantuan {
	// Runtime-sized pushes must reserve space; overflow is undefined behavior.
	inline void EnsureStackSpace(lua_State *L, int slots) {
		if (slots <= 0) {
			return;
		}

		if (!lua_checkstack(L, slots)) {
			luaL_error(L, "Cannot grow the Luau stack by %d slots", slots);
		}
	}

	// Use where luaL_error's longjmp cannot cross live C++ state.
	inline bool TryEnsureStackSpace(lua_State *L, int slots) {
		return slots <= 0 || lua_checkstack(L, slots) != 0;
	}

	class StackGuard {
	  public:
		explicit StackGuard(lua_State *L, int results = 0)
			: L(L), BaseTop(lua_gettop(L)), Results(results) {}

		~StackGuard() {
			if (!Dismissed && L) {
				lua_settop(L, BaseTop + Results);
			}
		}

		StackGuard(const StackGuard &) = delete;
		StackGuard &operator=(const StackGuard &) = delete;

		void Reserve(int slots) {
			EnsureStackSpace(L, slots);
		}

		void Dismiss() {
			Dismissed = true;
		}

		int Base() const {
			return BaseTop;
		}

		int Pushed() const {
			return lua_gettop(L) - BaseTop;
		}

	  private:
		lua_State *L;
		int BaseTop;
		int Results;
		bool Dismissed = false;
	};
}
