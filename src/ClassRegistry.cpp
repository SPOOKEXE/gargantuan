#include "gargantuan/ClassRegistry.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/classes/InputObject.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/classes/PointLight.hpp"
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
	namespace {
		struct Registry {
			std::unordered_map<std::type_index, Instance::ClassDefinition> Definitions;
			std::unordered_map<std::string_view, Instance::ClassDefinition *> ByName;
		};

		// Depth and the ancestor chain are derived from Superclass, so a class
		// has to be resolved after its parent. Recurse rather than making the
		// declaration order in Definitions load-bearing.
		void ResolveChain(Registry &registry, Instance::ClassDefinition &definition) {
			if (definition.Flattened) return;
			definition.Flattened = true;

			Instance::ClassDefinition *superclass = nullptr;
			if (definition.Superclass.has_value()) {
				auto it = registry.ByName.find(definition.Superclass.value());
				if (it == registry.ByName.end()) {
					SDL_Log(
						"Class %s names an unknown superclass %s",
						definition.Name.data(),
						definition.Superclass.value().data()
					);
				} else {
					superclass = it->second;
					ResolveChain(registry, *superclass);
				}
			}

			if (superclass) {
				definition.Depth = superclass->Depth + 1;
				if (definition.Depth >= Instance::MaxClassDepth) {
					SDL_Log("Class %s exceeds MaxClassDepth", definition.Name.data());
					definition.Depth = Instance::MaxClassDepth - 1;
				}
				definition.Ancestors = superclass->Ancestors;
				definition.AllProperties = superclass->AllProperties;
				definition.AllMethods = superclass->AllMethods;
			} else {
				definition.Depth = 0;
			}

			definition.Ancestors[definition.Depth] = definition.ClassId;

			// Own members last so a subclass overrides rather than loses.
			for (const auto &[name, property] : definition.Properties) {
				definition.AllProperties[name] = &property;
			}
			for (const auto &[name, method] : definition.Methods) {
				definition.AllMethods[name] = &method;
			}
		}

		Registry &Get() {
			static Registry *registry = [] {
				auto *self = new Registry{
					.Definitions =
						{
							USE_INSTANCE_DEFINITION(BasePart),
							USE_INSTANCE_DEFINITION(Camera),
							USE_INSTANCE_DEFINITION(DataModel),
							USE_INSTANCE_DEFINITION(InputObject),
							USE_INSTANCE_DEFINITION(Instance),
							USE_INSTANCE_DEFINITION(Lighting),
							USE_INSTANCE_DEFINITION(Part),
							USE_INSTANCE_DEFINITION(PointLight),
							USE_INSTANCE_DEFINITION(RunService),
							USE_INSTANCE_DEFINITION(ServiceProvider),
							USE_INSTANCE_DEFINITION(Tween),
							USE_INSTANCE_DEFINITION(TweenService),
							USE_INSTANCE_DEFINITION(UserInputService),
							USE_INSTANCE_DEFINITION(Workspace),
							USE_INSTANCE_DEFINITION(WorldRoot),
						},
				};

				uint16_t nextClassId = 0;
				for (auto &[type, definition] : self->Definitions) {
					definition.ClassId = nextClassId++;
					self->ByName[definition.Name] = &definition;
				}

				for (auto &[type, definition] : self->Definitions) {
					ResolveChain(*self, definition);
				}

				return self;
			}();
			return *registry;
		}
	} // namespace

	std::unordered_map<std::type_index, Instance::ClassDefinition> &GetDefinitionsMap() {
		return Get().Definitions;
	}

	Instance::ClassDefinition *GetDefinitionForType(const std::type_info &type) {
		auto &definitions = Get().Definitions;
		auto it = definitions.find(std::type_index(type));
		return it != definitions.end() ? &it->second : nullptr;
	}

	Instance::ClassDefinition *GetDefinition(Instance *instance) {
		if (!instance) return nullptr;
		return GetDefinitionForType(typeid(*instance));
	};

	Instance::ClassDefinition *GetDefinitionByName(std::string_view name) {
		auto &byName = Get().ByName;
		auto it = byName.find(name);
		return it != byName.end() ? it->second : nullptr;
	}

	std::vector<std::string_view> GetClassNames() {
		auto &byName = Get().ByName;
		std::vector<std::string_view> result;
		result.reserve(byName.size());
		for (auto &[name, definition] : byName) {
			result.emplace_back(name);
		}
		return result;
	}
} // namespace gargantuan::ClassRegistry
