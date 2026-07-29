#pragma once

#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <functional>
#include <lua.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace gargantuan {
	class Instance : public std::enable_shared_from_this<Instance>,
					 public Userdata<Instance, std::shared_ptr<Instance>> {
	  public:
		typedef std::shared_ptr<Instance> Pointer;
		typedef Userdata<Instance, std::shared_ptr<Instance>> This;
		G_UD_DECL_PRELUDE(Instance)

		struct ClassDefinition final {
			std::string_view Name;
			std::optional<std::string_view> Superclass;

			std::function<Pointer()> Constructor;
			template <typename T> static std::function<std::shared_ptr<Instance>()> WrapConstructor() {
				return []() -> std::shared_ptr<Instance> {
					auto instance = std::make_shared<T>();
					// Instance::Name's default initializer resolves to the base
					// class's DEFINITION, so the real class name is applied here
					instance->Name = T::DEFINITION.Name;
					return instance;
				};
			}

			std::unordered_map<std::string_view, This::Property> Properties = {};
			std::unordered_map<std::string_view, This::Method> Methods = {};
			bool Flattened = false;
			std::unordered_map<std::string_view, const This::Property *> AllProperties = {};
			std::unordered_map<std::string_view, const This::Method *> AllMethods = {};
		};

		static const ClassDefinition DEFINITION;

		virtual ~Instance() = default;

		std::string_view Name = DEFINITION.Name;
		bool Archivable = true;

		ClassDefinition *CachedDefinition = nullptr;

		// Stands in for the whole instance when all you need to know is "has
		// this changed since I last looked". Keep the value you saw and compare
		// it later; a difference means something was written.
		//
		// It is a counter rather than a hash of the properties, which is what
		// makes it cheap: no reading the properties, no comparing them, and no
		// per-property dirty flags to forget to set. The cost is that it means
		// "was written" rather than "is different", so assigning a value
		// identical to the current one still moves it.
		//
		// Deliberately small and deliberately allowed to wrap. A false match
		// needs exactly 65536 writes to land between two looks, and costs one
		// stale frame if it ever does -- cheaper than the wider counter and the
		// state comparison that would avoid it.
		uint16_t QuickHash = 0;

		std::vector<Instance *> *ChangeList = nullptr;
		uint32_t ChangeIndex = 0;
		bool InChangeList = false;

		// Call after changing this instance from C++, where the write did not
		// go through the property path that bumps it automatically
		void MarkChanged() {
			QuickHash++;
			if (ChangeList && !InChangeList) {
				InChangeList = true;
				ChangeIndex = (uint32_t)ChangeList->size();
				ChangeList->push_back(this);
			}
		}

		void LeaveChangeList() {
			if (!InChangeList) {
				return;
			}
			std::vector<Instance *> &list = *ChangeList;
			uint32_t last = (uint32_t)list.size() - 1;
			if (ChangeIndex != last) {
				list[ChangeIndex] = list[last];
				list[ChangeIndex]->ChangeIndex = ChangeIndex;
			}
			list.pop_back();
			InChangeList = false;
		}
		std::vector<std::shared_ptr<Instance>> Children;
		Instance *Parent = nullptr;
		void SetParent(std::shared_ptr<Instance> newParent);

		G_SIGNAL(ChildAdded, Instance::Pointer);
		G_SIGNAL(ChildRemoved, Instance::Pointer);
		G_SIGNAL(DescendantAdded, Instance::Pointer);
		G_SIGNAL(DescendantRemoved, Instance::Pointer);
		// (instance whose Parent changed, its new parent). Aliased because the
		// comma in the tuple would otherwise split the macro argument
		typedef std::tuple<Instance::Pointer, Instance::Pointer> AncestryChangedArguments;
		G_SIGNAL(AncestryChanged, AncestryChangedArguments);
		G_SIGNAL(Destroying, std::monostate);

		template <typename T> bool IsClass() const {
			return dynamic_cast<const T *>(this) != nullptr;
		}
		template <typename T> T *Cast() const {
			return dynamic_cast<const T *>(this);
		}
		template <typename T> T *Cast() {
			return dynamic_cast<T *>(this);
		}
		template <typename T> const T *Cast() const {
			return dynamic_cast<const T *>(this);
		}

		const This::Property *FindProperty(std::string_view name);
		const This::Method *FindMethod(std::string_view name);

		static int UserdataIndex(lua_State *L);
		static int UserdataNewIndex(lua_State *L);
		static int UserdataNamecall(lua_State *L);
		static int LEq(lua_State *L, Instance *self);

		std::string GetFullName();
		bool IsA(std::string_view className);
		std::vector<std::shared_ptr<Instance>> &GetChildren();
		std::vector<std::shared_ptr<Instance>> GetDescendants();
		std::shared_ptr<Instance> FindFirstChild(std::string_view name, bool recursive = false);
		std::shared_ptr<Instance> FindFirstChildOfClass(std::string_view className);
		std::shared_ptr<Instance> FindFirstChildWhichIsA(std::string_view className, bool recursive = false);
		std::shared_ptr<Instance> FindFirstDescendant(std::string_view name);
		std::shared_ptr<Instance> FindFirstDescendantOfClass(std::string_view className);
		std::shared_ptr<Instance> FindFirstDescendantWhichIsA(std::string_view className);
		std::shared_ptr<Instance> FindFirstAncestor(std::string_view name);
		std::shared_ptr<Instance> FindFirstAncestorOfClass(std::string_view className);
		std::shared_ptr<Instance> FindFirstAncestorWhichIsA(std::string_view className);

		bool IsAncestorOf(std::shared_ptr<Instance> descendant);
		bool IsDescendantOf(std::shared_ptr<Instance> ancestor);
		void ClearAllChildren();
		void Destroy();
		bool IsDestroyed() const;

	  private:
		// A destroyed instance is permanently unparented; Roblox locks the
		// Parent property rather than letting it be re-attached
		bool Destroyed = false;

		void CollectDescendants(std::vector<std::shared_ptr<Instance>> &descendants);
		void FireAncestryChanged(std::shared_ptr<Instance> child, std::shared_ptr<Instance> parent);
	};

	// Hand-written rather than G_UD_STACKVALUE_WITH_STORED so that a null
	// instance crosses into Luau as nil instead of a userdata wrapping nullptr
	template <> struct StackValue<Instance::Pointer> {
		typedef Userdata<Instance, Instance::Pointer> This;

		// Optional, because a null instance crosses into Luau as nil in both
		// directions
		static inline std::string ReflectedTypedef() {
			return std::string(StackValue<This>::ReflectedTypedef()) + "?";
		};

		static bool Is(lua_State *L, int idx) {
			return StackValue<This>::Is(L, idx);
		};

		static Instance::Pointer From(lua_State *L, int idx) {
			if (lua_isnoneornil(L, idx)) {
				return nullptr;
			}
			return StackValue<This>::From(L, idx);
		};

		static int Push(lua_State *L, Instance::Pointer value) {
			if (!value) {
				lua_pushnil(L);
				return 1;
			}
			return StackValue<This>::Push(L, value);
		};
	};

	template <typename Subclass>
		requires std::is_base_of_v<Instance, Subclass>
	struct StackValue<std::shared_ptr<Subclass>> {
	  public:
		static inline std::string ReflectedTypedef() {
			return std::string(ClassName()) + "?";
		};

		static bool Is(lua_State *L, int idx) {
			if (!StackValue<Instance::Pointer>::Is(L, idx)) return false;
			auto instance = StackValue<Instance::Pointer>::From(L, idx);
			return instance->IsA(ClassName());
		};

		// NOTE: a pointer cast that shares ownership. Building a shared_ptr from
		// the raw Cast<Subclass>() result would hand out a second owner of an
		// already-owned instance and free it twice.
		static std::shared_ptr<Subclass> From(lua_State *L, int idx) {
			auto instance = gargantuan::StackValue<Instance::Pointer>::From(L, idx);
			return std::dynamic_pointer_cast<Subclass>(instance);
		};

		static int Push(lua_State *L, std::shared_ptr<Subclass> value) {
			return gargantuan::StackValue<Instance::Pointer>::Push(L, value);
		};

	  private:
		// NOTE: read at call time, not as a constexpr member -- DEFINITION is a
		// runtime-initialised object, so a constexpr copy fails to compile the
		// moment this specialisation is actually instantiated
		static std::string_view ClassName() {
			return Subclass::DEFINITION.Name;
		}
	};
} // namespace gargantuan
