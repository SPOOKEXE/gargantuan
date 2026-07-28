#include "gargantuan/ClassRegistry.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/classes/ServiceProvider.hpp"
#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/services/RunService.hpp"
#include "gargantuan/services/TweenService.hpp"
#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/services/Workspace.hpp"

#include <SDL3/SDL_log.h>
#include <cstddef>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#define USE_INSTANCE_DEFINITION(instance) {typeid(instance), instance::DEFINITION}

namespace gargantuan::ClassRegistry {
	std::unordered_map<std::type_index, Instance::ClassDefinition> &GetDefinitionsMap() {
		static auto *CLASS_DEFINITIONS = new std::unordered_map<std::type_index, Instance::ClassDefinition>{
			USE_INSTANCE_DEFINITION(BasePart),
			USE_INSTANCE_DEFINITION(Camera),
			USE_INSTANCE_DEFINITION(DataModel),
			USE_INSTANCE_DEFINITION(InputObject),
			USE_INSTANCE_DEFINITION(Instance),
			USE_INSTANCE_DEFINITION(Lighting),
			USE_INSTANCE_DEFINITION(Part),
			USE_INSTANCE_DEFINITION(RunService),
			USE_INSTANCE_DEFINITION(ServiceProvider),
			USE_INSTANCE_DEFINITION(Tween),
			USE_INSTANCE_DEFINITION(TweenService),
			USE_INSTANCE_DEFINITION(UserInputService),
			USE_INSTANCE_DEFINITION(Workspace),
			USE_INSTANCE_DEFINITION(WorldRoot),
		};
		return *CLASS_DEFINITIONS;
	}

	Instance::ClassDefinition *GetDefinition(Instance *instance) {
		if (!instance) return nullptr;
		auto &map = GetDefinitionsMap();
		auto it = map.find(std::type_index(typeid(*instance)));
		if (it != map.end()) {
			return &it->second;
		}
		return nullptr;
	};

	Instance::ClassDefinition *GetDefinitionByName(std::string_view name) {
		auto &map = GetDefinitionsMap();
		for (auto &definition : map) {
			if (definition.second.Name == name) {
				return &definition.second;
			}
		}
		return nullptr;
	}

	std::vector<std::string_view> GetClassNames() {
		auto &map = GetDefinitionsMap();
		std::vector<std::string_view> result;
		result.reserve(map.size());
		for (auto &definition : map) {
			result.emplace_back(definition.second.Name);
		}
		return result;
	}
} // namespace gargantuan::ClassRegistry
