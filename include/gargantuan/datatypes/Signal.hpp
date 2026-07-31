#pragma once

#include "gargantuan/reflection/Enums.hpp"
#include "gargantuan/scripting/StackValue.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include "gargantuan/scripting/UserdataTag.hpp"

#include <SDL3/SDL_log.h>
#include <any>
#include <cstddef>
#include <functional>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string_view>
#include <vector>

#define G_SIGNAL(propertyName, signalType)                                                                             \
	Signal<signalType>::Pointer propertyName = std::make_shared<Signal<signalType>>();

namespace gargantuan {
	G_ENUM(SignalType, Engine, User);

	struct SignalConnection : public Userdata<SignalConnection, std::shared_ptr<SignalConnection>>,
							  public std::enable_shared_from_this<SignalConnection> {
	  public:
		G_UD_DECL_PRELUDE(SignalConnection)

		typedef std::shared_ptr<SignalConnection> Pointer;
		typedef Userdata<SignalConnection, std::shared_ptr<SignalConnection>> This;

		typedef std::any CallbackArgument;
		typedef std::function<void(CallbackArgument)> CallbackType;

		SignalConnection(CallbackType callback, lua_State *L = nullptr, int callbackReference = LUA_NOREF);

		CallbackType Callback;
		lua_State *L;
		int CallbackReference;
		bool Connected = true;

		void Disconnect();

		static int LGarbageCollect(lua_State *L, SignalConnection *signal);
	};

	struct BaseSignal : public Userdata<BaseSignal, std::shared_ptr<BaseSignal>>,
						public std::enable_shared_from_this<BaseSignal> {
	  public:
		G_UD_DECL_PRELUDE(BaseSignal)

		typedef std::shared_ptr<BaseSignal> Pointer;
		typedef Userdata<BaseSignal, Pointer> This;

		typedef std::any CallbackArgument;
		typedef std::function<void(CallbackArgument)> CallbackType;

		std::vector<SignalConnection::Pointer> Connections;
		int FiringDepth = 0;

	  protected:
		SignalConnection::Pointer
		Connect(CallbackType callback, lua_State *L = nullptr, int callbackReference = LUA_NOREF);
		SignalConnection::Pointer
		Once(CallbackType callback, lua_State *L = nullptr, int callbackReference = LUA_NOREF);
		void Fire(CallbackArgument argument);

		virtual int LPushArgument(lua_State *L, CallbackArgument value) = 0;

		static int LConnect(lua_State *L, BaseSignal *signal);
		static int LOnce(lua_State *L, BaseSignal *signal);
		static int LWait(lua_State *L, BaseSignal *signal);
		static int LFire(lua_State *L, BaseSignal *signal);

		static int LReferenceCallback(lua_State *L, int idx);
		static void LRunCallback(lua_State *L, BaseSignal *signal, int callbackReference, std::any value);

		virtual Enums::SignalType GetSignalType() = 0;
	};

	template <typename T> struct Signal : BaseSignal {
		typedef std::shared_ptr<Signal> Pointer;
		typedef Userdata<Signal, Pointer> This;

		typedef T CallbackArgument;
		typedef std::function<void(T)> CallbackType;

		Enums::SignalType GetSignalType() override {
			return Enums::SignalType::Engine;
		}

		SignalConnection::Pointer
		Connect(CallbackType callback, lua_State *L = nullptr, int callbackReference = LUA_NOREF) {
			return BaseSignal::Connect(
				[callback](std::any value) { callback(std::any_cast<T>(value)); }, L, callbackReference
			);
		}

		SignalConnection::Pointer
		Once(CallbackType callback, lua_State *L = nullptr, int callbackReference = LUA_NOREF) {
			return BaseSignal::Once(
				[callback](std::any value) { callback(std::any_cast<T>(value)); }, L, callbackReference
			);
		}

		void Fire(T argument) {
			BaseSignal::Fire(std::any(argument));
		}

		int LPushArgument(lua_State *L, std::any value) override {
			return StackValue<T>::Push(L, std::any_cast<T>(value));
		};

		static std::string_view GetUserdataType() {
			return BaseSignal::GetUserdataType();
		};
		static UserdataTag GetUserdataTag() {
			return BaseSignal::GetUserdataTag();
		};
		static const BaseSignal::UserdataProperties &GetUserdataProperties() {
			return BaseSignal::GetUserdataProperties();
		};
		static const BaseSignal::UserdataMethods &GetUserdataMethods() {
			return BaseSignal::GetUserdataMethods();
		};
	};

	struct UserSignal : BaseSignal {
		typedef std::shared_ptr<UserSignal> Pointer;
		typedef Userdata<UserSignal, Pointer> This;

		Enums::SignalType GetSignalType() override {
			return Enums::SignalType::User;
		}

		int LPushArgument(lua_State *L, std::any value) override;
	};

	G_UD_STACKVALUE_WITH_STORED(SignalConnection, SignalConnection::Pointer)
	G_UD_STACKVALUE_WITH_STORED(BaseSignal, BaseSignal::Pointer)

	template <typename T> struct StackValue<std::shared_ptr<Signal<T>>> {
		static inline std::string_view ReflectedTypedef() {
			return StackValue<BaseSignal::Pointer>::ReflectedTypedef();
		};
		static bool Is(lua_State *L, int idx) {
			return StackValue<BaseSignal::Pointer>::Is(L, idx);
		};
		static std::shared_ptr<Signal<T>> From(lua_State *L, int idx) {
			auto baseSignal = StackValue<BaseSignal::Pointer>::From(L, idx);
			return std::dynamic_pointer_cast<Signal<T>>(baseSignal);
		};
		static int Push(lua_State *L, std::shared_ptr<Signal<T>> value) {
			return StackValue<BaseSignal::Pointer>::Push(L, std::static_pointer_cast<BaseSignal>(value));
		};
	};

	template <> struct StackValue<std::shared_ptr<UserSignal>> {
		static inline std::string_view ReflectedTypedef() {
			return StackValue<BaseSignal::Pointer>::ReflectedTypedef();
		};
		static bool Is(lua_State *L, int idx) {
			return StackValue<BaseSignal::Pointer>::Is(L, idx);
		};
		static std::shared_ptr<UserSignal> From(lua_State *L, int idx) {
			auto baseSignal = StackValue<BaseSignal::Pointer>::From(L, idx);
			return std::dynamic_pointer_cast<UserSignal>(baseSignal);
		};
		static int Push(lua_State *L, std::shared_ptr<UserSignal> value) {
			return StackValue<BaseSignal::Pointer>::Push(L, std::static_pointer_cast<BaseSignal>(value));
		};
	};
}
