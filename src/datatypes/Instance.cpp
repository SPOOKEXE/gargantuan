#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ClassRegistry.hpp"
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
	// Instances reach Luau as a fresh userdata every push, so identity
	// comparison would never hold; compare the instances they point at
	G_UD_IMPL_METHODS(Instance, {"__eq", {&Instance::LEq}});

	int Instance::LEq(lua_State *L, Instance *self) {
		if (!StackValue<Instance::Pointer>::Is(L, 2)) {
			lua_pushboolean(L, false);
			return 1;
		}

		Instance::Pointer other = StackValue<Instance::Pointer>::From(L, 2);
		lua_pushboolean(L, other.get() == self);
		return 1;
	};

	const Instance::ClassDefinition Instance::DEFINITION = {
		.Name = "Instance",
		.Properties =
			{
				G_UD_READWRITE_PROP(Instance, Name, std::string_view),
				G_UD_READWRITE_PROP(Instance, Archivable, bool),
				{
					// Read-only: a script writing it could only lie to whoever
					// is watching for a change
					"QuickHash",
					{
						+[](lua_State *L, Instance *instance) -> int {
							StackValue<int>::Push(L, (int)instance->QuickHash);
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(int),
					},
				},
				G_UD_READONLY_PROP(Instance, ChildAdded, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(Instance, ChildRemoved, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(Instance, DescendantAdded, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(Instance, DescendantRemoved, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(Instance, AncestryChanged, Signal<AncestryChangedArguments>::Pointer),
				G_UD_READONLY_PROP(Instance, Destroying, Signal<std::monostate>::Pointer),
				{
					"ClassName",
					{
						+[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string_view>::Push(L, ClassRegistry::GetDefinition(instance)->Name);
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(std::string_view),
					},
				},
				{
					"Parent",
					{
						+[](lua_State *L, Instance *instance) -> int {
							if (auto parent = instance->Parent) {
								StackValue<Instance::Userdata>::Push(L, parent->shared_from_this());
							} else {
								lua_pushnil(L);
							};
							return 1;
						},
						+[](lua_State *L, Instance *instance) -> int {
							// Parent = nil is how Luau detaches an instance
							if (lua_isnoneornil(L, -1)) {
								instance->SetParent(nullptr);
								return 0;
							}

							Instance::Pointer newParent = CheckStackValue<Instance::Pointer>(L, -1);
							instance->SetParent(newParent);
							return 0;
						},
						G_UD_REFLECT_TYPE(Instance::Pointer),
					},
				},
			},
		.Methods = {
			{"IsA", Method::Wrap<&Instance::IsA>()},
			{"GetFullName", Method::Wrap<&Instance::GetFullName>()},
			{"GetChildren", Method::Wrap<&Instance::GetChildren>()},
			{"GetDescendants", Method::Wrap<&Instance::GetDescendants>()},
			{"FindFirstChild", Method::Wrap<&Instance::FindFirstChild>()},
			{"FindFirstChildOfClass", Method::Wrap<&Instance::FindFirstChildOfClass>()},
			{"FindFirstChildWhichIsA", Method::Wrap<&Instance::FindFirstChildWhichIsA>()},
			{"FindFirstDescendant", Method::Wrap<&Instance::FindFirstDescendant>()},
			{"FindFirstDescendantOfClass", Method::Wrap<&Instance::FindFirstDescendantOfClass>()},
			{"FindFirstDescendantWhichIsA", Method::Wrap<&Instance::FindFirstDescendantWhichIsA>()},
			{"FindFirstAncestor", Method::Wrap<&Instance::FindFirstAncestor>()},
			{"FindFirstAncestorOfClass", Method::Wrap<&Instance::FindFirstAncestorOfClass>()},
			{"FindFirstAncestorWhichIsA", Method::Wrap<&Instance::FindFirstAncestorWhichIsA>()},
			{"IsAncestorOf", Method::Wrap<&Instance::IsAncestorOf>()},
			{"IsDescendantOf", Method::Wrap<&Instance::IsDescendantOf>()},
			{"ClearAllChildren", Method::Wrap<&Instance::ClearAllChildren>()},
			{"Destroy", Method::Wrap<&Instance::Destroy>()},
		}
	};

	void Instance::SetParent(std::shared_ptr<Instance> newParent) {
		// Destroy permanently detaches an instance, so its parent stays locked
		if (Destroyed || Parent == newParent.get()) {
			return;
		}

		std::shared_ptr<Instance> self = shared_from_this();

		// This whole subtree leaves the old ancestry and joins the new one, so
		// collect it once up front and reuse it for both sets of signals
		std::vector<std::shared_ptr<Instance>> subtree = {self};
		CollectDescendants(subtree);

		if (Parent != nullptr) {
			auto &oldChildren = Parent->Children;
			if (auto it = std::find(oldChildren.begin(), oldChildren.end(), self); it != oldChildren.end()) {
				oldChildren.erase(it);
				Parent->ChildRemoved->Fire(self);
			}

			for (Instance *ancestor = Parent; ancestor != nullptr; ancestor = ancestor->Parent) {
				for (auto &node : subtree) {
					ancestor->DescendantRemoved->Fire(node);
				}
			}
		}

		Parent = newParent.get();

		if (newParent != nullptr) {
			newParent->Children.push_back(self);
			newParent->ChildAdded->Fire(self);

			for (Instance *ancestor = newParent.get(); ancestor != nullptr; ancestor = ancestor->Parent) {
				for (auto &node : subtree) {
					ancestor->DescendantAdded->Fire(node);
				}
			}
		}

		FireAncestryChanged(self, newParent);
	}

	// AncestryChanged fires on the moved instance and on everything beneath it,
	// always reporting the instance whose Parent actually changed
	void Instance::FireAncestryChanged(std::shared_ptr<Instance> child, std::shared_ptr<Instance> parent) {
		AncestryChanged->Fire({child, parent});
		for (auto &descendant : Children) {
			descendant->FireAncestryChanged(child, parent);
		}
	}

	void Instance::ClearAllChildren() {
		// Destroy detaches from Children as it goes, so iterate a copy
		auto children = Children;
		for (auto &child : children) {
			child->Destroy();
		}
	}

	void Instance::Destroy() {
		if (Destroyed) {
			return;
		}

		Destroying->Fire({});

		auto children = Children;
		for (auto &child : children) {
			child->Destroy();
		}

		SetParent(nullptr);
		Destroyed = true;
	}

	bool Instance::IsDestroyed() const {
		return Destroyed;
	}

	const Instance::Userdata::Property *Instance::FindProperty(std::string_view name) {
		const ClassDefinition *definition = ClassRegistry::GetDefinition(this);
		if (!definition) {
			return nullptr;
		}

		auto it = definition->AllProperties.find(name);
		return it != definition->AllProperties.end() ? it->second : nullptr;
	}

	const Instance::Userdata::Method *Instance::FindMethod(std::string_view name) {
		const ClassDefinition *definition = ClassRegistry::GetDefinition(this);
		if (!definition) {
			return nullptr;
		}

		auto it = definition->AllMethods.find(name);
		return it != definition->AllMethods.end() ? it->second : nullptr;
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
					// Every scripted property write funnels through here, so
					// this one bump covers the lot
					instance->MarkChanged();
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

	bool Instance::IsA(std::string_view className) {
		auto currentDefinition = ClassRegistry::GetDefinition(this);
		while (currentDefinition) {
			if (currentDefinition->Name == className) {
				return true;
			}

			auto superclass = currentDefinition->Superclass;
			if (superclass.has_value()) {
				currentDefinition = ClassRegistry::GetDefinitionByName(superclass.value());
			} else {
				return false;
			}
		}

		return false;
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

	std::shared_ptr<Instance> Instance::FindFirstChild(std::string_view name, bool recursive) {
		for (const auto &child : Children) {
			if (child->Name == name) {
				return child;
			}
		};

		if (recursive) {
			for (const auto &child : Children) {
				if (auto found = child->FindFirstChild(name, true)) {
					return found;
				}
			}
		}

		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstChildOfClass(std::string_view className) {
		for (const auto &child : Children) {
			auto definition = ClassRegistry::GetDefinition(child.get());
			if (definition && definition->Name == className) {
				return child;
			}
		};
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstChildWhichIsA(std::string_view className, bool recursive) {
		for (const auto &child : Children) {
			if (child->IsA(className)) {
				return child;
			}
		};

		if (recursive) {
			for (const auto &child : Children) {
				if (auto found = child->FindFirstChildWhichIsA(className, true)) {
					return found;
				}
			}
		}

		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendant(std::string_view name) {
		for (const auto &descendant : GetDescendants()) {
			if (descendant->Name == name) {
				return descendant;
			}
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantOfClass(std::string_view className) {
		for (const auto &descendant : GetDescendants()) {
			auto definition = ClassRegistry::GetDefinition(descendant.get());
			if (definition && definition->Name == className) {
				return descendant;
			}
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstDescendantWhichIsA(std::string_view className) {
		for (const auto &descendant : GetDescendants()) {
			if (descendant->IsA(className)) {
				return descendant;
			}
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstAncestor(std::string_view name) {
		for (Instance *ancestor = Parent; ancestor != nullptr; ancestor = ancestor->Parent) {
			if (ancestor->Name == name) {
				return ancestor->shared_from_this();
			}
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstAncestorOfClass(std::string_view className) {
		for (Instance *ancestor = Parent; ancestor != nullptr; ancestor = ancestor->Parent) {
			auto definition = ClassRegistry::GetDefinition(ancestor);
			if (definition && definition->Name == className) {
				return ancestor->shared_from_this();
			}
		}
		return nullptr;
	}

	std::shared_ptr<Instance> Instance::FindFirstAncestorWhichIsA(std::string_view className) {
		for (Instance *ancestor = Parent; ancestor != nullptr; ancestor = ancestor->Parent) {
			if (ancestor->IsA(className)) {
				return ancestor->shared_from_this();
			}
		}
		return nullptr;
	}

	bool Instance::IsAncestorOf(std::shared_ptr<Instance> descendant) {
		if (!descendant) {
			return false;
		}
		return descendant->IsDescendantOf(shared_from_this());
	}

	bool Instance::IsDescendantOf(std::shared_ptr<Instance> ancestor) {
		if (!ancestor) {
			return false;
		}

		for (Instance *current = Parent; current != nullptr; current = current->Parent) {
			if (current == ancestor.get()) {
				return true;
			}
		}
		return false;
	}
} // namespace gargantuan
