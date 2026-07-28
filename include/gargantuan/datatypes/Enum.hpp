#pragma once

#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL_log.h>
#include <lua.h>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gargantuan {
	struct Enum;
	namespace Enums {
		std::unordered_map<std::string_view, std::shared_ptr<Enum>> &GetEnums();
	}

	struct EnumItem : Userdata<EnumItem> {
		G_UD_DECL_PRELUDE(EnumItem)

		std::string_view Name;
		int Value = 0;
		std::shared_ptr<Enum> EnumType;

		// Default-constructible so a missing argument can produce an empty one
		// instead of dereferencing null
		EnumItem() = default;
		EnumItem(std::string_view name, int value, std::shared_ptr<Enum> enumType)
			: Name(name), Value(value), EnumType(std::move(enumType)) {};

		static int LTostring(lua_State *L, EnumItem *self);
		static int LEq(lua_State *L, EnumItem *self);
	};

	struct Enum : Userdata<Enum, std::shared_ptr<Enum>> {
		typedef std::shared_ptr<Enum> Pointer;
		typedef Userdata<Enum, Pointer> This;

		G_UD_DECL_PRELUDE(Enum)
		std::string_view Name;
		std::vector<EnumItem> Items;

		template <typename E>
			requires std::is_enum_v<E>
		static typename Enum::Pointer fromType() {
			static const Enum::Pointer self = []() {
				Enum::Pointer result = std::make_shared<Enum>();
				result->Name = magic_enum::enum_type_name<E>();
				SDL_Log("Building enum %.*s", static_cast<int>(result->Name.size()), result->Name.data());

				constexpr auto entries = magic_enum::enum_entries<E>();
				result->Items.reserve(entries.size());

				for (const auto &[value, name] : entries) {
					result->Items.emplace_back(name, static_cast<int>(value), result);
				}

				SDL_Log("Finished building enum %.*s", static_cast<int>(result->Name.size()), result->Name.data());
				return result;
			}();
			return self;
		}

		std::vector<EnumItem> &GetEnumItems();
		std::optional<EnumItem> FromName(std::string_view name);
		std::optional<EnumItem> FromValue(int value);
		static int LIndex(lua_State *L, Enum *self);
		static int LTostring(lua_State *L, Enum *self);
		static int LEq(lua_State *L, Enum *self);
	};

	G_UD_STACKVALUE(EnumItem);
	G_UD_STACKVALUE_WITH_STORED(Enum, Enum::Pointer);
}
