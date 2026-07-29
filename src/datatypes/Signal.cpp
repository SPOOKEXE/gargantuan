#include "gargantuan/Profiler.hpp"
#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/scripting/Userdata.hpp"
#include "gargantuan/scripting/UserdataTag.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(SignalConnection);
	G_UD_IMPL_PROPS(
		SignalConnection,

		{"Connected", Property::fromSimple<&SignalConnection::Connected>(true, false)}
	);
	G_UD_IMPL_METHODS(
		SignalConnection,

		G_UD_METHOD(SignalConnection, Disconnect),
		{"__gc", {&SignalConnection::LGarbageCollect}}
	);

	SignalConnection::SignalConnection(CallbackType callback, lua_State *L, int callbackReference)
		: Callback(std::move(callback)), L(L), CallbackReference(callbackReference), Connected(true) {}

	// Idempotent: releasing the Luau reference is guarded on its own, so this is
	// safe to call on a connection that is already marked disconnected
	void SignalConnection::Disconnect() {
		Connected = false;
		if (L && CallbackReference != LUA_NOREF && CallbackReference != LUA_REFNIL) {
			lua_State *mainState = lua_mainthread(L);
			lua_unref(mainState, CallbackReference);
			CallbackReference = LUA_NOREF;
			L = nullptr;
		}
	}

	int SignalConnection::LGarbageCollect(lua_State *L, SignalConnection *self) {
		if (self) {
			self->Disconnect();
		}
		return 0;
	}

	std::string_view BaseSignal::GetUserdataType() {
		return "Signal";
	};

	UserdataTag BaseSignal::GetUserdataTag() {
		return UserdataTag::Signal;
	};

	G_UD_IMPL_PROPS(
		BaseSignal,

		{"Type", Property::fromRead([](BaseSignal *self) { return self->GetSignalType(); })}
	);
	G_UD_IMPL_METHODS(
		BaseSignal,

		{"Connect", {BaseSignal::LConnect}},
		{"Once", {BaseSignal::LOnce}},
		{"Wait", {BaseSignal::LWait}},
		{"Fire", {BaseSignal::LFire}},
	)

	SignalConnection::Pointer
	BaseSignal::Connect(std::function<void(std::any)> callback, lua_State *L, int callbackReference) {
		auto connection = std::make_shared<SignalConnection>(callback, L, callbackReference);
		Connections.push_back(connection);
		return connection;
	};

	SignalConnection::Pointer
	BaseSignal::Once(std::function<void(std::any)> callback, lua_State *L, int callbackReference) {
		auto connection = std::make_shared<SignalConnection>(nullptr, L, callbackReference);
		std::weak_ptr<SignalConnection> weakConnection = connection;
		connection->Callback = [weakConnection, callback](CallbackArgument value) {
			auto conn = weakConnection.lock();

			// Mark it spent before running so a re-entrant fire cannot call it
			// twice, but hold onto the Luau reference until the call is done --
			// Disconnect releases it, and the callback still needs it
			if (conn) {
				conn->Connected = false;
			}

			if (callback) {
				callback(value);
			}

			if (conn) {
				conn->Disconnect();
			}
		};
		Connections.push_back(connection);
		return connection;
	};

	void BaseSignal::Fire(CallbackArgument value) {
		// A handler may connect, disconnect or fire this signal again, so run
		// over a snapshot rather than the live list
		auto connections = Connections;

		for (auto &connection : connections) {
			if (connection && connection->Connected && connection->Callback) {
				connection->Callback(value);
			}
		}

		std::erase_if(Connections, [](const SignalConnection::Pointer &connection) {
			return !connection || !connection->Connected;
		});
	}

	int BaseSignal::LConnect(lua_State *L, BaseSignal *signal) {
		int callbackReference = LReferenceCallback(L, 2);
		return StackValue<SignalConnection::Pointer>::Push(
			L,
			signal->Connect(
				[L, callbackReference, signal](CallbackArgument value) {
					LRunCallback(L, signal, callbackReference, value);
				},
				L,
				callbackReference
			)
		);
	}

	int BaseSignal::LOnce(lua_State *L, BaseSignal *signal) {
		int callbackReference = LReferenceCallback(L, 2);
		return StackValue<SignalConnection::Pointer>::Push(
			L,
			signal->Once(
				[L, callbackReference, signal](CallbackArgument value) {
					LRunCallback(L, signal, callbackReference, value);
				},
				L,
				callbackReference
			)
		);
	}

	int BaseSignal::LWait(lua_State *L, BaseSignal *signal) {
		signal->Once(
			[L, signal](CallbackArgument value) {
				int argumentCount = signal->LPushArgument(L, value);
				int status = lua_resume(L, nullptr, argumentCount);
				if (status != LUA_OK && status != LUA_YIELD) {
					SDL_Log("Failed to resume thread after signal: %s", lua_tostring(L, -1));
					lua_pop(L, 1);
				};
			},
			L,
			LUA_NOREF
		);
		return lua_yield(L, 0);
	}

	int BaseSignal::LFire(lua_State *L, BaseSignal *signal) {
		// TODO: This should be on a per-signal basis, ie. you might wanna fire
		// RunService.PreRender on the server or smshit
		if (signal->GetSignalType() != Enums::SignalType::User) {
			luaL_error(L, "Cannot fire Signals created by the engine");
			return 0;
		}

		auto stackCount = lua_gettop(L);
		auto argumentCount = std::max(stackCount - 1, 0);
		// Every argument gets pushed again to be referenced
		EnsureStackSpace(L, 1);

		auto argumentVector = std::make_shared<std::vector<int>>();
		argumentVector->reserve(argumentCount);

		for (int i = 2; i <= stackCount; ++i) {
			lua_pushvalue(L, i);
			int ref = lua_ref(L, -1);
			lua_pop(L, 1);
			argumentVector->push_back(ref);
		}

		signal->Fire(argumentVector);

		for (int ref : *argumentVector) {
			lua_unref(L, ref);
		}

		return 0;
	}

	int UserSignal::LPushArgument(lua_State *L, std::any value) {
		if (!value.has_value()) {
			return 0;
		}

		auto argumentsPointer = std::any_cast<std::shared_ptr<std::vector<int>>>(&value);
		if (!argumentsPointer || !*argumentsPointer) {
			return 0;
		}

		lua_State *mainState = lua_mainthread(L);
		auto &arguments = **argumentsPointer;

		// The argument count comes from the script, so make room for it up
		// front; a signal fired with thousands of values must not run off the
		// end of the stack
		int slots = (int)arguments.size();
		if (!TryEnsureStackSpace(mainState, slots + 1) || (L != mainState && !TryEnsureStackSpace(L, slots + 1))) {
			SDL_Log("Dropping a signal firing with %d arguments: the Luau stack cannot grow that far", slots);
			return 0;
		}

		int pushedCount = 0;
		for (int ref : arguments) {
			lua_getref(mainState, ref);

			if (L != mainState) {
				lua_xmove(mainState, L, 1);
			}

			pushedCount++;
		}

		return pushedCount;
	}

	int BaseSignal::LReferenceCallback(lua_State *L, int idx) {
		if (!lua_isfunction(L, idx)) {
			luaL_typeerror(L, idx, "function");
		}

		lua_pushvalue(L, idx);
		int ref = lua_ref(L, -1);
		lua_pop(L, 1);

		return ref;
	}

	void BaseSignal::LRunCallback(lua_State *L, BaseSignal *signal, int callbackReference, std::any value) {
		if (callbackReference == LUA_NOREF || callbackReference == LUA_REFNIL) {
			return;
		}

		lua_State *mainState = lua_mainthread(L);
		lua_getref(mainState, callbackReference);

		if (!lua_isfunction(mainState, -1)) {
			lua_pop(mainState, 1);
			return;
		}

		// Labelled with whichever script the handler was written in, taken off
		// the function itself rather than asked for. A place with several
		// scripts connected to PreRender otherwise reports one lump of time
		// with nothing to say about which of them is spending it, and the
		// answer is already sitting in the function's debug info.
		std::string label;
		if (G_PROFILE_ACTIVE()) {
			lua_Debug info;
			if (lua_getinfo(mainState, -1, "s", &info) && info.short_src) {
				label = info.short_src;

				// Luau reports a chunk loaded from a buffer as [string "Name"],
				// which is three quarters punctuation in a chart row that is
				// already short of width
				constexpr std::string_view WRAPPER = "[string \"";
				if (label.starts_with(WRAPPER) && label.ends_with("\"]")) {
					label = label.substr(WRAPPER.size(), label.size() - WRAPPER.size() - 2);
				}
			} else {
				label = "?";
			}
		}

		int arguments = signal->LPushArgument(mainState, value);
		if (lua_pcall(mainState, arguments, 0, 0) != LUA_OK) {
			SDL_Log("Signal error: %s", lua_tostring(mainState, -1));
			lua_pop(mainState, 1);
		}
	}
} // namespace gargantuan
