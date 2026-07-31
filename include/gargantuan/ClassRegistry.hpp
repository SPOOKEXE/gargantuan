#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/scripting/StackValue.hpp"

#include <memory>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace gargantuan::ClassRegistry {
	std::unordered_map<std::type_index, Instance::ClassDefinition> &GetDefinitionsMap();

	// Declared in Instance.hpp so IsA<T>() can reach it without a cycle.
	Instance::ClassDefinition *GetDefinitionForType(const std::type_info &type);

	template <typename T> Instance::ClassDefinition *GetDefinition() {
		return GetDefinitionForType(typeid(T));
	}

	Instance::ClassDefinition *GetDefinition(Instance *instance);
	Instance::ClassDefinition *GetDefinitionByName(std::string_view name);
	std::vector<std::string_view> GetClassNames();
} // namespace gargantuan::ClassRegistry
