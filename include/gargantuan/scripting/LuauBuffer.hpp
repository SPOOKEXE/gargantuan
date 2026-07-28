#pragma once

#include "gargantuan/scripting/StackValue.hpp"

#include <cstring>
#include <lua.h>

namespace gargantuan {
	// A view onto the bytes of a Luau buffer. Borrowed, not owned -- the
	// buffer it points at stays alive only as long as it is on the Luau stack
	// or otherwise reachable from Luau.
	struct LuauBuffer {
		void *Data = nullptr;
		size_t Size = 0;
	};

	template <> struct StackValue<LuauBuffer> {
		static inline std::string_view ReflectedTypedef() {
			return "buffer";
		};

		static bool Is(lua_State *L, int idx) {
			return lua_isbuffer(L, idx);
		};

		static LuauBuffer From(lua_State *L, int idx) {
			size_t size = 0;
			void *data = lua_tobuffer(L, idx, &size);
			return {data, size};
		};

		// Allocates a fresh Luau buffer and copies the bytes into it, so the
		// result does not alias whatever the view pointed at
		static int Push(lua_State *L, LuauBuffer value) {
			void *data = lua_newbuffer(L, value.Size);
			if (value.Data != nullptr && value.Size > 0) {
				std::memcpy(data, value.Data, value.Size);
			}
			return 1;
		};
	};
} // namespace gargantuan
