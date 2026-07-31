#pragma once

#include "gargantuan/scripting/StackValue.hpp"

#include <cstring>
#include <lua.h>

namespace gargantuan {
	// Borrowed; valid only while the Luau buffer remains reachable.
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

		static int Push(lua_State *L, LuauBuffer value) {
			void *data = lua_newbuffer(L, value.Size);
			if (value.Data != nullptr && value.Size > 0) {
				std::memcpy(data, value.Data, value.Size);
			}
			return 1;
		};
	};
}
