#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/scripting/StackValue.hpp"

#include <memory>
#include <string_view>
#include <typeindex>
#include <vector>

namespace gargantuan::ClassRegistry {
	std::unordered_map<std::type_index, Instance::ClassDefinition> &GetDefinitionsMap();

	Instance::ClassDefinition *GetDefinitionByType(std::type_index type);

	template <typename T> Instance::ClassDefinition *GetDefinition() {
		return GetDefinitionByType(std::type_index(typeid(T)));
	}

	Instance::ClassDefinition *GetDefinition(Instance *instance);
	Instance::ClassDefinition *GetDefinitionByName(std::string_view name);
	std::vector<std::string_view> GetClassNames();
} // namespace gargantuan::ClassRegistry
