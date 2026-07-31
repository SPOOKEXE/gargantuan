#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ClassRegistry.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstddef>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Instance);
	G_UD_IMPL_PROPS(Instance);
	G_UD_IMPL_METHODS(Instance);

	const Instance::ClassDefinition Instance::DEFINITION = {
		.Name = "Instance",
		.Properties =
			{
				{"Name", Property::fromSimple<&Instance::Name>(true, true)},
				{
					"ClassName",
					Property::fromRead([](Instance *instance) -> std::string_view {
						return instance->GetClassDefinition().Name;
					}),
				},
				{
					"Parent",
					// Writable with nil, which is how an instance is taken out
					// of the tree -- and how a registry learns to drop its row.
					Property::fromReadWrite<std::optional<Instance::Pointer>>(
						[](Instance *instance) -> std::optional<Instance::Pointer> {
							// nullopt, not an engaged optional holding null:
							// the latter pushes a userdata wrapping a null
							// pointer instead of nil.
							if (!instance->Parent) return std::nullopt;
							return instance->Parent->shared_from_this();
						},
						[](Instance *instance, std::optional<Instance::Pointer> newParent) {
							instance->SetParent(newParent.value_or(nullptr));
						}
					),
				},
				{
					"ChildAdded",
					Property::fromRead([](Instance *instance) { return instance->GetChildAdded(); }),
				},
				{
					"ChildRemoved",
					Property::fromRead([](Instance *instance) { return instance->GetChildRemoved(); }),
				},
				{
					"DescendantAdded",
					Property::fromRead([](Instance *instance) { return instance->GetDescendantAdded(); }),
				},
				{
					"DescendantRemoved",
					Property::fromRead([](Instance *instance) { return instance->GetDescendantRemoved(); }),
				},
			},
		.Methods = {
			{"IsA", Method::Wrap<(bool (Instance::*)(std::string_view) const) & Instance::IsA>()},
			{"Destroy", Method::Wrap<&Instance::Destroy>()},
			{"GetFullName", Method::Wrap<&Instance::GetFullName>()},
			{"GetChildren", Method::Wrap<&Instance::GetChildren>()},
			{"GetDescendants", Method::Wrap<&Instance::GetDescendants>()},
			{"FindFirstChild", Method::Wrap<&Instance::FindFirstChild>()},
			{"FindFirstChildOfClass", Method::Wrap<&Instance::FindFirstChildOfClass>()},
		}
	};

	Instance::SignalBlock &Instance::EnsureSignals() {
		if (!Signals) {
			Signals = std::make_unique<SignalBlock>();
		}
		return *Signals;
	}

#define G_LAZY_SIGNAL(name)                                                                                            \
	Signal<Instance::Pointer>::Pointer &Instance::Get##name() {                                                        \
		auto &block = EnsureSignals();                                                                                 \
		if (!block.name) block.name = std::make_shared<Signal<Instance::Pointer>>();                                   \
		return block.name;                                                                                             \
	}

	G_LAZY_SIGNAL(ChildAdded)
	G_LAZY_SIGNAL(ChildRemoved)
	G_LAZY_SIGNAL(DescendantAdded)
	G_LAZY_SIGNAL(DescendantRemoved)

#undef G_LAZY_SIGNAL

	const Instance::ClassDefinition &Instance::GetClassDefinition() const {
		if (!CachedDefinition) {
			CachedDefinition = ClassRegistry::GetDefinitionForType(typeid(*this));
			// A class that was never registered still needs to answer IsA and
			// ClassName without crashing.
			if (!CachedDefinition) CachedDefinition = &Instance::DEFINITION;
		}
		return *CachedDefinition;
	}

	bool Instance::IsA(std::string_view className) const {
		const ClassDefinition *target = ClassRegistry::GetDefinitionByName(className);
		return target != nullptr && IsA(*target);
	}

	// Fires DescendantAdded/DescendantRemoved for this instance and everything
	// under it, at every ancestor from `from` upwards. Firing the whole subtree
	// is what lets a registry track parts nested inside models rather than only
	// direct children of the world.
	void Instance::FireDescendantSignals(Instance *from, bool added) {
		bool anyListener = false;
		for (Instance *ancestor = from; ancestor; ancestor = ancestor->Parent) {
			if (!ancestor->Signals) continue;
			if (added ? (bool)ancestor->Signals->DescendantAdded : (bool)ancestor->Signals->DescendantRemoved) {
				anyListener = true;
				break;
			}
		}
		if (!anyListener) return;

		std::vector<Pointer> subtree;
		subtree.push_back(shared_from_this());
		CollectDescendants(subtree);

		for (Instance *ancestor = from; ancestor; ancestor = ancestor->Parent) {
			if (!ancestor->Signals) continue;
			auto &signal = added ? ancestor->Signals->DescendantAdded : ancestor->Signals->DescendantRemoved;
			if (!signal) continue;
			for (auto &node : subtree) {
				signal->Fire(node);
			}
		}
	}

	void Instance::SetParent(std::shared_ptr<Instance> newParent) {
		std::shared_ptr<Instance> self = shared_from_this();

		if (Parent != nullptr) {
			auto &oldChildren = Parent->Children;
			if (auto it = std::find(oldChildren.begin(), oldChildren.end(), self); it != oldChildren.end()) {
				oldChildren.erase(it);
				// Still linked to the old ancestors here, deliberately: the
				// removal has to be announced before the chain is broken.
				FireDescendantSignals(Parent, false);
				if (Parent->Signals && Parent->Signals->ChildRemoved) {
					Parent->Signals->ChildRemoved->Fire(self);
				}
			}
		}

		Parent = newParent.get();

		if (newParent != nullptr) {
			newParent->Children.push_back(self);
			if (newParent->Signals && newParent->Signals->ChildAdded) {
				newParent->Signals->ChildAdded->Fire(self);
			}
			FireDescendantSignals(newParent.get(), true);
		}

		MarkChanged(ecs::ChangeFlags::Hierarchy);
	}

	const Instance::Userdata::Property *Instance::FindProperty(std::string_view name) const {
		const ClassDefinition &definition = GetClassDefinition();
		auto it = definition.AllProperties.find(name);
		return it != definition.AllProperties.end() ? it->second : nullptr;
	}

	const Instance::Userdata::Method *Instance::FindMethod(std::string_view name) const {
		const ClassDefinition &definition = GetClassDefinition();
		auto it = definition.AllMethods.find(name);
		return it != definition.AllMethods.end() ? it->second : nullptr;
	}

	int Instance::UserdataIndex(lua_State *L) {
		Instance::Pointer instance = CheckStackValue<Instance::Pointer>(L, 1);
		const char *key = luaL_checkstring(L, 2);

		if (key && instance) {
			const auto *property = instance->FindProperty(key);
			if (property) {
				if (property->Read) {
					lua_remove(L, 1);
					lua_remove(L, 1);
					return property->Read(L, instance.get());
				} else {
					luaL_error(L, "Property %s is write-only", key);
				}
			} else if (auto child = instance->FindFirstChild(key)) {
				lua_settop(L, 0);
				StackValue<Instance::Pointer>::Push(L, child);
				return 1;
			}
		}

		return 0;
	};

	int Instance::UserdataNewIndex(lua_State *L) {
		Instance::Pointer instance = CheckStackValue<Instance::Pointer>(L, 1);
		const char *key = luaL_checkstring(L, 2);

		if (key && instance) {
			const auto *property = instance->FindProperty(key);
			if (property) {
				if (property->Write) {
					// The assigned value is the top of the stack, which is
					// where the stack-based writers expect to find it.
					return property->Write(L, instance.get());
				} else {
					luaL_error(L, "Property %s is read-only", key);
				}
			}
		}

		luaL_error(L, "Unknown property %s", key);

		return 0;
	};

	int Instance::UserdataNamecall(lua_State *L) {
		Instance::Pointer instance = CheckStackValue<Instance::Pointer>(L, 1);
		const char *key = lua_namecallatom(L, nullptr);

		if (key && instance) {
			const auto *method = instance->FindMethod(key);
			if (method) {
				return method->Call(L, instance.get());
			}
		}

		luaL_error(L, "%s is not a valid method of %s", key, instance->Name.data());
		return 0;
	};

	void Instance::Destroy() {
		if (Destroyed) return;
		Destroyed = true;

		// Unparent first: the descendant signals this fires are how registries
		// learn that the whole subtree is leaving.
		SetParent(nullptr);

		auto children = Children;
		for (auto &child : children) {
			child->Destroy();
		}
		Children.clear();

		// Disconnecting is what releases each handler's registry reference; the
		// signals going out of scope on their own would leave them pinned.
		if (Signals) {
			for (auto &signal :
				 {Signals->ChildAdded, Signals->ChildRemoved, Signals->DescendantAdded, Signals->DescendantRemoved}) {
				if (!signal) {
					continue;
				}
				for (const auto &connection : signal->Connections) {
					if (connection) {
						connection->Disconnect();
					}
				}
				signal->Connections.clear();
			}
		}
	}

	std::string Instance::GetFullName() {
		std::vector<std::string_view> path;

		size_t totalLength = 0;
		Instance *current = this;

		while (current) {
			auto &name = current->Name;
			path.push_back(name);
			totalLength += name.size() + 1;
			current = current->Parent;
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

	std::shared_ptr<Instance> Instance::FindFirstChild(std::string_view name, bool recursive) {
		for (const auto &child : Children) {
			if (child->Name == name) {
				return child;
			}
		};
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstChildOfClass(std::string_view className) {
		for (const auto &child : Children) {
			if (child->GetClassDefinition().Name == className) {
				return child;
			}
		};
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendant(std::string_view name) {
		for (const auto &child : Children) {
			if (child->Name == name) return child;
			if (auto found = child->FindFirstDescendant(name)) return found;
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantOfClass(std::string_view className) {
		for (const auto &child : Children) {
			if (child->GetClassDefinition().Name == className) return child;
			if (auto found = child->FindFirstDescendantOfClass(className)) return found;
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantWhichIsA(std::string_view className) {
		const ClassDefinition *target = ClassRegistry::GetDefinitionByName(className);
		if (!target) return nullptr;
		for (const auto &child : Children) {
			if (child->IsA(*target)) return child;
			if (auto found = child->FindFirstDescendantWhichIsA(className)) return found;
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstChildWhichIsA(std::string_view className) {
		const ClassDefinition *target = ClassRegistry::GetDefinitionByName(className);
		if (!target) return nullptr;
		for (const auto &child : Children) {
			if (child->IsA(*target)) {
				return child;
			}
		}
		return nullptr;
	}
} // namespace gargantuan
