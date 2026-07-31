#pragma once

#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceArena.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <array>
#include <functional>
#include <lua.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace gargantuan {
	class Instance : public std::enable_shared_from_this<Instance>,
					 public Userdata<Instance, std::shared_ptr<Instance>> {
	  public:
		typedef std::shared_ptr<Instance> Pointer;
		typedef Userdata<Instance, std::shared_ptr<Instance>> This;
		G_UD_DECL_PRELUDE(Instance)

		static constexpr size_t MaxClassDepth = 16;

		struct ClassDefinition final {
			std::string_view Name;
			std::optional<std::string_view> Superclass;

			std::function<Pointer()> Constructor;
			template <typename T> static std::function<std::shared_ptr<Instance>()> WrapConstructor();

			std::shared_ptr<ecs::InstanceArena> Arena = std::make_shared<ecs::InstanceArena>();

			std::unordered_map<std::string_view, This::Property> Properties = {};
			std::unordered_map<std::string_view, This::Method> Methods = {};


			uint16_t ClassId = 0;
			uint8_t Depth = 0;
			std::array<uint16_t, MaxClassDepth> Ancestors = {};

			bool Flattened = false;
			std::unordered_map<std::string_view, const This::Property *> AllProperties = {};
			std::unordered_map<std::string_view, const This::Method *> AllMethods = {};
		};

		static const ClassDefinition DEFINITION;

		virtual ~Instance() = default;

		std::string Name = std::string(DEFINITION.Name);
		std::vector<std::shared_ptr<Instance>> Children;
		Instance *Parent = nullptr;
		void SetParent(std::shared_ptr<Instance> newParent);

		uint32_t WorldIndex = ecs::InvalidIndex;
		ecs::RegistryBase *Registry = nullptr;

		uint16_t QuickHash = 0;

		void MarkChanged(ecs::ChangeFlags flags = ecs::ChangeFlags::All) {
			QuickHash++;
			if (Registry) Registry->Mark(WorldIndex, flags);
		}

		struct SignalBlock {
			Signal<Pointer>::Pointer ChildAdded;
			Signal<Pointer>::Pointer ChildRemoved;
			Signal<Pointer>::Pointer DescendantAdded;
			Signal<Pointer>::Pointer DescendantRemoved;
		};

		Signal<Pointer>::Pointer &GetChildAdded();
		Signal<Pointer>::Pointer &GetChildRemoved();
		Signal<Pointer>::Pointer &GetDescendantAdded();
		Signal<Pointer>::Pointer &GetDescendantRemoved();

		const ClassDefinition &GetClassDefinition() const;

		bool IsA(const ClassDefinition &target) const {
			const ClassDefinition &self = GetClassDefinition();
			return self.Depth >= target.Depth && self.Ancestors[target.Depth] == target.ClassId;
		}
		bool IsA(std::string_view className) const;
		template <typename T> bool IsA() const;

		template <typename T> bool IsClass() const {
			return IsA<T>();
		}
		template <typename T> T *Cast() {
			return IsA<T>() ? static_cast<T *>(this) : nullptr;
		}
		template <typename T> const T *Cast() const {
			return IsA<T>() ? static_cast<const T *>(this) : nullptr;
		}

		const This::Property *FindProperty(std::string_view name) const;
		const This::Method *FindMethod(std::string_view name) const;

		static int UserdataIndex(lua_State *L);
		static int UserdataNewIndex(lua_State *L);
		static int UserdataNamecall(lua_State *L);

		void Destroy();
		bool IsDestroyed() const {
			return Destroyed;
		}

		std::string GetFullName();
		std::vector<std::shared_ptr<Instance>> &GetChildren();
		std::vector<std::shared_ptr<Instance>> GetDescendants();
		std::shared_ptr<Instance> FindFirstChild(std::string_view name, bool recursive = false);
		std::shared_ptr<Instance> FindFirstChildOfClass(std::string_view className);
		std::shared_ptr<Instance> FindFirstChildWhichIsA(std::string_view className);
		std::shared_ptr<Instance> FindFirstDescendant(std::string_view name);
		std::shared_ptr<Instance> FindFirstDescendantOfClass(std::string_view className);
		std::shared_ptr<Instance> FindFirstDescendantWhichIsA(std::string_view className);

	  private:
		std::unique_ptr<SignalBlock> Signals;
		mutable const ClassDefinition *CachedDefinition = nullptr;
		bool Destroyed = false;

		SignalBlock &EnsureSignals();
		void CollectDescendants(std::vector<std::shared_ptr<Instance>> &descendants);
		void FireDescendantSignals(Instance *from, bool added);
	};

	namespace ClassRegistry {
		Instance::ClassDefinition *GetDefinitionForType(const std::type_info &type);
	}

	template <typename T> bool Instance::IsA() const {
		static const ClassDefinition *target = ClassRegistry::GetDefinitionForType(typeid(T));
		return target != nullptr && IsA(*target);
	}

	template <typename T> std::function<std::shared_ptr<Instance>()> Instance::ClassDefinition::WrapConstructor() {
		return []() -> std::shared_ptr<Instance> {
			ClassDefinition *definition = ClassRegistry::GetDefinitionForType(typeid(T));
			if (!definition || !definition->Arena) {
				return std::make_shared<T>();
			}
			return std::allocate_shared<T>(ecs::InstanceAllocator<T>(definition->Arena.get()));
		};
	}

	G_UD_STACKVALUE_WITH_STORED(Instance, Instance::Pointer)

	template <typename Subclass>
		requires std::is_base_of_v<Instance, Subclass>
	struct StackValue<std::shared_ptr<Subclass>> {
	  public:
		static inline std::string_view ReflectedTypedef() {
			return Subclass::DEFINITION.Name;
		};

		static bool Is(lua_State *L, int idx) {
			if (!StackValue<Instance::Pointer>::Is(L, idx)) return false;
			auto instance = StackValue<Instance::Pointer>::From(L, idx);
			return instance && instance->IsA<Subclass>();
		};

		static std::shared_ptr<Subclass> From(lua_State *L, int idx) {
			auto instance = gargantuan::StackValue<Instance::Pointer>::From(L, idx);
			if (!instance || !instance->IsA<Subclass>()) return nullptr;
			return std::static_pointer_cast<Subclass>(instance);
		};

		static int Push(lua_State *L, std::shared_ptr<Subclass> value) {
			return gargantuan::StackValue<Instance::Pointer>::Push(L, value);
		};
	};
}
