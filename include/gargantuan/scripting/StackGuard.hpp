#pragma once

#include <lua.h>
#include <lualib.h>

namespace gargantuan {
	// Grows the Luau stack by `slots` or raises a Luau error. Call this before
	// pushing a number of values that is decided at runtime -- pushing past the
	// stack limit is otherwise undefined behaviour rather than a clean error.
	inline void EnsureStackSpace(lua_State *L, int slots) {
		if (slots <= 0) {
			return;
		}

		if (!lua_checkstack(L, slots)) {
			luaL_error(L, "Cannot grow the Luau stack by %d slots", slots);
		}
	}

	// Same, but reports failure instead of raising, for paths that cannot
	// longjmp (destructors, signal dispatch, anything holding C++ state)
	inline bool TryEnsureStackSpace(lua_State *L, int slots) {
		return slots <= 0 || lua_checkstack(L, slots) != 0;
	}

	// Restores the stack to the depth it had on construction, plus however many
	// results the scope promised to leave. Guards against both leaking slots
	// and popping past the caller's values.
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

		// Reserve room for values about to be pushed within this scope
		void Reserve(int slots) {
			EnsureStackSpace(L, slots);
		}

		// Leave the stack exactly as it is when the scope ends
		void Dismiss() {
			Dismissed = true;
		}

		// Depth the stack had when the guard was taken
		int Base() const {
			return BaseTop;
		}

		// How many values have been pushed since then
		int Pushed() const {
			return lua_gettop(L) - BaseTop;
		}

	  private:
		lua_State *L;
		int BaseTop;
		int Results;
		bool Dismissed = false;
	};
} // namespace gargantuan
