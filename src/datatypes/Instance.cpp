#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/reflection/InstanceClassRegistry.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include "gargantuan/scripting/UserdataTag.hpp"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstddef>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gargantuan {
	G_USERDATA_IMPL(
		Instance,
		.Tag = UserdataTag::Instance,
		.Type = "Instance",
		.Methods = {
			{"__index", Method{&Instance::LIndex}},
			{"__newindex", Method{&Instance::LNewIndex}},
			{"__namecall", Method{&Instance::LNamecall}},
		}
	);
	G_INSTANCE_IMPL(
		Instance,
		.Superclass = std::nullopt,
		.Properties =
			{
				{"Name", Property::fromMember<&Instance::Name>(true, true)},
				{
					"ClassName",
					Property::fromRead([](Instance *instance) -> std::string_view {
						return InstanceClassRegistry::GetDefinition(instance)->ClassName;
					}),
				},
				{
					"Parent",
					Property::fromReadWrite<std::optional<Instance::Pointer>>(
						[](Instance *instance) -> std::optional<Instance::Pointer> {
							auto parent = instance->GetParent();
							return parent ? std::optional(parent) : std::nullopt;
						},
						[](Instance *instance, std::optional<Instance::Pointer> newParent) {
							instance->SetParent(newParent.value_or(nullptr));
						}
					),
				},
				{"ChildAdded", Property::fromReadonlyMember<&Instance::ChildAdded>()},
				{"ChildRemoved", Property::fromReadonlyMember<&Instance::ChildRemoved>()},
				{"DescendantAdded", Property::fromReadonlyMember<&Instance::DescendantAdded>()},
				{"DescendantRemoved", Property::fromReadonlyMember<&Instance::DescendantRemoved>()},
			},
		.Methods = {
			{"IsA", Method::fromMember<&Instance::IsA>()},
			{"GetFullName", Method::fromMember<&Instance::GetFullName>()},
			{"GetChildren", Method::fromMember<&Instance::GetChildren>()},
			{"GetDescendants", Method::fromMember<&Instance::GetDescendants>()},
			{"FindFirstChild", Method::fromMember<&Instance::FindFirstChild>()},
			{"FindFirstChildOfClass", Method::fromMember<&Instance::FindFirstChildOfClass>()},
			{"FindFirstChildWhichIsA", Method::fromMember<&Instance::FindFirstChildWhichIsA>()},
			{"FindFirstDescendant", Method::fromMember<&Instance::FindFirstDescendant>()},
			{"FindFirstDescendantOfClass", Method::fromMember<&Instance::FindFirstDescendantOfClass>()},
			{"FindFirstDescendantWhichIsA", Method::fromMember<&Instance::FindFirstDescendantWhichIsA>()},
			{"Destroy", Method::fromMember<&Instance::Destroy>()},
		}
	);

	std::shared_ptr<Instance> Instance::GetParent() {
		return ParentReference.lock();
	}

	namespace {
		void FireForSubtree(const Signal<Instance::Pointer>::Pointer &signal, const Instance::Pointer &node) {
			signal->Fire(node);
			for (const auto &child : node->Children) {
				FireForSubtree(signal, child);
			}
		}
	}

	void Instance::SetParent(std::shared_ptr<Instance> newParent) {
		std::shared_ptr<Instance> self = shared_from_this();
		std::shared_ptr<Instance> oldParent = GetParent();

		if (oldParent == newParent) {
			return;
		}

		if (Destroyed && newParent != nullptr) {
			return;
		}

		if (oldParent) {
			auto &oldChildren = oldParent->Children;
			if (auto it = std::find(oldChildren.begin(), oldChildren.end(), self); it != oldChildren.end()) {
				oldChildren.erase(it);
				ParentReference.reset();

				oldParent->ChildRemoved->Fire(self);
				for (auto ancestor = oldParent; ancestor; ancestor = ancestor->GetParent()) {
					FireForSubtree(ancestor->DescendantRemoved, self);
				}
			}
		}

		ParentReference = newParent;

		if (newParent != nullptr) {
			newParent->Children.push_back(self);

			newParent->ChildAdded->Fire(self);
			for (auto ancestor = newParent; ancestor; ancestor = ancestor->GetParent()) {
				FireForSubtree(ancestor->DescendantAdded, self);
			}
		}
	}

	const Instance::Self::Property *Instance::FindProperty(std::string_view name) {
		const InstanceClassDefinition *definition = InstanceClassRegistry::GetDefinition(this);
		if (!definition) {
			return nullptr;
		}

		auto it = definition->AllProperties.find(name);
		return it != definition->AllProperties.end() ? it->second : nullptr;
	}

	const Instance::Self::Method *Instance::FindMethod(std::string_view name) {
		const InstanceClassDefinition *definition = InstanceClassRegistry::GetDefinition(this);
		if (!definition) {
			return nullptr;
		}

		auto it = definition->AllMethods.find(name);
		return it != definition->AllMethods.end() ? it->second : nullptr;
	}

	int Instance::LIndex(lua_State *L, Instance *self) {
		const char *key = luaL_checkstring(L, 2);

		if (key && self) {
			const auto *property = self->FindProperty(key);
			if (property) {
				if (property->Read) {
					// lua_remove(L, 1);
					// lua_remove(L, 1);
					return property->PushStack(L, property->Read(self));
				} else {
					luaL_error(L, "Property %s is write-only", key);
				}
			} else if (auto child = self->FindFirstChild(key)) {
				// lua_settop(L, 0);
				StackValue<Instance::Pointer>::Push(L, child);
				return 1;
			}
		}

		return 0;
	};

	int Instance::LNewIndex(lua_State *L, Instance *self) {
		const char *key = luaL_checkstring(L, 2);

		if (key && self) {
			const auto *property = self->FindProperty(key);
			if (property) {
				if (property->Write) {
					auto value = property->CheckStack(L, 3);
					property->Write(self, value);
					return 0;
				} else {
					luaL_error(L, "Property %s is read-only", key);
				}
			}
		}

		luaL_error(L, "Unknown property %s", key);

		return 0;
	};

	int Instance::LNamecall(lua_State *L, Instance *self) {
		const char *key = lua_namecallatom(L, nullptr);

		if (key && self) {
			const auto *method = self->FindMethod(key);
			if (method) {
				return method->Call(L, self);
			}
		}

		luaL_error(L, "%s is not a valid method of %s", key, self->Name.data());
		return 0;
	};

	std::string Instance::GetFullName() {
		std::vector<std::string_view> path;

		size_t totalLength = 0;

		path.push_back(Name);
		totalLength += Name.size() + 1;

		for (auto current = GetParent(); current; current = current->GetParent()) {
			auto &name = current->Name;
			path.push_back(name);
			totalLength += name.size() + 1;
		};

		if (path.empty()) {
			return "";
		}

		if (totalLength > 0) {
			totalLength--;
		}

		std::string fullName;
		fullName.reserve(totalLength);

		auto begin = path.rbegin();
		for (auto it = begin; it != path.rend(); ++it) {
			if (it != begin) {
				fullName.push_back('.');
			}
			fullName.append(*it);
		}

		return fullName;
	};

	bool Instance::IsA(std::string_view className) {
		auto currentDefinition = InstanceClassRegistry::GetDefinition(this);
		while (true) {
			if (currentDefinition->ClassName == className) {
				return true;
			}

			auto superclass = currentDefinition->Superclass;
			if (superclass.has_value()) {
				currentDefinition = InstanceClassRegistry::GetDefinitionByName(superclass.value());
			} else {
				return false;
			}
		}
	}

	std::vector<std::shared_ptr<Instance>> &Instance::GetChildren() {
		return Children;
	}

	void Instance::CollectDescendants(std::vector<std::shared_ptr<Instance>> &descendants) {
		for (const auto &child : Children) {
			descendants.push_back(child);
			child->CollectDescendants(descendants);
		}
	}

	std::vector<std::shared_ptr<Instance>> Instance::GetDescendants() {
		std::vector<std::shared_ptr<Instance>> descendants;
		CollectDescendants(descendants);
		return descendants;
	}

	namespace {
		template <typename Matches>
		Instance::Pointer FindNearest(const std::vector<Instance::Pointer> &children, Matches matches, bool recursive) {
			for (const auto &child : children) {
				if (matches(child)) {
					return child;
				}
			}

			if (recursive) {
				for (const auto &child : children) {
					if (auto found = FindNearest(child->Children, matches, true)) {
						return found;
					}
				}
			}

			return nullptr;
		}

		auto MatchesName(std::string_view name) {
			return [name](const Instance::Pointer &child) { return child->Name == name; };
		}

		auto MatchesClass(std::string_view className) {
			return [className](const Instance::Pointer &child) {
				return InstanceClassRegistry::GetDefinition(child.get())->ClassName == className;
			};
		}

		auto MatchesIsA(std::string_view className) {
			return [className](const Instance::Pointer &child) { return child->IsA(className); };
		}
	}

	std::shared_ptr<Instance> Instance::FindFirstChild(std::string_view name, bool recursive) {
		return FindNearest(Children, MatchesName(name), recursive);
	}

	std::shared_ptr<Instance> Instance::FindFirstChildOfClass(std::string_view className) {
		return FindNearest(Children, MatchesClass(className), false);
	}

	std::shared_ptr<Instance> Instance::FindFirstChildWhichIsA(std::string_view className) {
		return FindNearest(Children, MatchesIsA(className), false);
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendant(std::string_view name) {
		return FindNearest(Children, MatchesName(name), true);
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantOfClass(std::string_view className) {
		return FindNearest(Children, MatchesClass(className), true);
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantWhichIsA(std::string_view className) {
		return FindNearest(Children, MatchesIsA(className), true);
	}

	void Instance::Destroy() {
		SetParent(nullptr);
		Destroyed = true;

		// A copy, because each child takes itself out of Children as it goes
		auto children = Children;
		for (const auto &child : children) {
			child->Destroy();
		}

		for (const auto &signal : {ChildAdded, ChildRemoved, DescendantAdded, DescendantRemoved}) {
			for (const auto &connection : signal->Connections) {
				if (connection) {
					connection->Disconnect();
				}
			}
			signal->Connections.clear();
		}
	}
}
