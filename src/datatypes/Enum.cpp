#include "gargantuan/datatypes/Enum.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(EnumItem);
	G_UD_IMPL_PROPS(
		EnumItem,

		{"Name", Property::fromSimple<&EnumItem::Name>(true, false)},
		{"Value", Property::fromSimple<&EnumItem::Value>(true, false)},
		{"EnumType", Property::fromSimple<&EnumItem::EnumType>(true, false)}
	);
	G_UD_IMPL_METHODS(EnumItem, {"__tostring", {&EnumItem::LTostring}}, {"__eq", {&EnumItem::LEquals}});

	int EnumItem::LEquals(lua_State *L, EnumItem *self) {
		// Luau only dispatches __eq when both operands share this metatable, so
		// the second operand is always an EnumItem here.
		EnumItem other = StackValue<EnumItem>::From(L, 2);
		bool equal = self->Value == other.Value && self->EnumType && other.EnumType &&
					 self->EnumType->Name == other.EnumType->Name;
		lua_pushboolean(L, equal);
		return 1;
	};

	int EnumItem::LTostring(lua_State *L, EnumItem *self) {
		std::ostringstream ss;
		ss << "Enum." << self->EnumType->Name << "." << self->Name;
		std::string str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	};

	G_UD_IMPL_PRELUDE(Enum);
	G_UD_IMPL_PROPS(Enum);
	G_UD_IMPL_METHODS(
		Enum,

		G_UD_METHOD(Enum, GetEnumItems),
		G_UD_METHOD(Enum, FromName),
		G_UD_METHOD(Enum, FromValue),
		{"__index", {&Enum::LIndex}},
		{"__tostring", {&Enum::LTostring}}
	);

	std::vector<EnumItem> &Enum::GetEnumItems() {
		return Items;
	};

	std::optional<EnumItem> Enum::FromName(std::string_view name) {
		for (auto &item : Items) {
			if (item.Name == name) {
				return item;
			}
		}
		return {};
	};

	std::optional<EnumItem> Enum::FromValue(int value) {
		for (auto &item : Items) {
			if (item.Value == value) {
				return item;
			}
		}
		return {};
	};

	int Enum::LIndex(lua_State *L, Enum *self) {
		auto key = luaL_checkstring(L, 2);
		if (auto item = self->FromName(key)) {
			StackValue<EnumItem>::Push(L, item.value());
			return 1;
		} else {
			luaL_errorL(L, "%s is not a valid member of \"Enum.%s\"", key, self->Name.data());
			return 0;
		}
	};

	int Enum::LTostring(lua_State *L, Enum *self) {
		StackValue<std::string_view>::Push(L, self->Name);
		return 1;
	};
}
