#include "gargantuan/ClassRegistry.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/classes/ShaderProperties.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/classes/ServiceProvider.hpp"
#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/services/AssetService.hpp"
#include "gargantuan/services/RenderSettings.hpp"
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
			USE_INSTANCE_DEFINITION(AssetService),
			USE_INSTANCE_DEFINITION(BasePart),
			USE_INSTANCE_DEFINITION(Camera),
			USE_INSTANCE_DEFINITION(ComputeShader),
			USE_INSTANCE_DEFINITION(DataModel),
			USE_INSTANCE_DEFINITION(EditableImage),
			USE_INSTANCE_DEFINITION(InputObject),
			USE_INSTANCE_DEFINITION(Instance),
			USE_INSTANCE_DEFINITION(Lighting),
			USE_INSTANCE_DEFINITION(Part),
			USE_INSTANCE_DEFINITION(PostProcessShader),
			USE_INSTANCE_DEFINITION(RenderSettings),
			USE_INSTANCE_DEFINITION(RunService),
			USE_INSTANCE_DEFINITION(ServiceProvider),
			USE_INSTANCE_DEFINITION(ShaderProperties),
			USE_INSTANCE_DEFINITION(ShaderScript),
			USE_INSTANCE_DEFINITION(SurfaceShader),
			USE_INSTANCE_DEFINITION(Tween),
			USE_INSTANCE_DEFINITION(TweenService),
			USE_INSTANCE_DEFINITION(UserInputService),
			USE_INSTANCE_DEFINITION(Workspace),
			USE_INSTANCE_DEFINITION(WorldRoot),
		};
		return *CLASS_DEFINITIONS;
	}

	namespace {
		std::unordered_map<std::string_view, Instance::ClassDefinition *> &GetNameIndex() {
			static auto *NAME_INDEX = [] {
				auto *index = new std::unordered_map<std::string_view, Instance::ClassDefinition *>();
				for (auto &entry : GetDefinitionsMap()) {
					index->emplace(entry.second.Name, &entry.second);
				}
				return index;
			}();
			return *NAME_INDEX;
		}

		void Flatten(Instance::ClassDefinition *definition) {
			for (Instance::ClassDefinition *current = definition; current;) {
				for (auto &[name, property] : current->Properties) {
					definition->AllProperties.emplace(name, &property);
				}
				for (auto &[name, method] : current->Methods) {
					definition->AllMethods.emplace(name, &method);
				}

				if (!current->Superclass.has_value()) {
					break;
				}
				current = GetDefinitionByName(current->Superclass.value());
			}

			definition->Flattened = true;
		}
	} // namespace

	Instance::ClassDefinition *GetDefinitionByType(std::type_index type) {
		auto &map = GetDefinitionsMap();
		auto it = map.find(type);
		if (it == map.end()) {
			return nullptr;
		}

		Instance::ClassDefinition *definition = &it->second;
		if (!definition->Flattened) {
			Flatten(definition);
		}
		return definition;
	}

	Instance::ClassDefinition *GetDefinition(Instance *instance) {
		if (!instance) return nullptr;
		if (instance->CachedDefinition) {
			return instance->CachedDefinition;
		}

		instance->CachedDefinition = GetDefinitionByType(std::type_index(typeid(*instance)));
		return instance->CachedDefinition;
	};

	Instance::ClassDefinition *GetDefinitionByName(std::string_view name) {
		auto &index = GetNameIndex();
		auto it = index.find(name);
		return it != index.end() ? it->second : nullptr;
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
