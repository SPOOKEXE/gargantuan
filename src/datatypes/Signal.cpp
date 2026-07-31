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
#include <string_view>
#include <vector>

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

	// The main thread, not the caller's: a coroutine that connects and then
	// finishes is collected, and the reference outlives it.
	SignalConnection::SignalConnection(CallbackType callback, lua_State *L, int callbackReference)
		: Callback(std::move(callback)), L(L ? lua_mainthread(L) : nullptr), CallbackReference(callbackReference),
		  Connected(true) {}

	// Not guarded on Connected: Once clears it before running the handler and
	// disconnects afterwards, so a guard here would strand the reference.
	void SignalConnection::Disconnect() {
		Connected = false;
		if (L && CallbackReference != LUA_NOREF && CallbackReference != LUA_REFNIL) {
			lua_unref(L, CallbackReference);
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
		size_t count = Connections.size();
		FiringDepth++;

		for (size_t index = 0; index < count; index++) {
			auto connection = Connections[index];
			if (connection && connection->Connected && connection->Callback) {
				connection->Callback(value);
			}
		}

		FiringDepth--;
		if (FiringDepth == 0) {
			std::erase_if(Connections, [](const SignalConnection::Pointer &connection) {
				return !connection || !connection->Connected;
			});
		}
	}

	int BaseSignal::LConnect(lua_State *L, BaseSignal *signal) {
		int callbackReference = LReferenceCallback(L, 2);
		lua_State *mainState = lua_mainthread(L);

		return StackValue<SignalConnection::Pointer>::Push(
			L,
			signal->Connect(
				[mainState, callbackReference, signal](CallbackArgument value) {
					LRunCallback(mainState, signal, callbackReference, value);
				},
				mainState,
				callbackReference
			)
		);
	}

	int BaseSignal::LOnce(lua_State *L, BaseSignal *signal) {
		int callbackReference = LReferenceCallback(L, 2);
		lua_State *mainState = lua_mainthread(L);

		return StackValue<SignalConnection::Pointer>::Push(
			L,
			signal->Once(
				[mainState, callbackReference, signal](CallbackArgument value) {
					LRunCallback(mainState, signal, callbackReference, value);
				},
				mainState,
				callbackReference
			)
		);
	}

	int BaseSignal::LWait(lua_State *L, BaseSignal *signal) {
		lua_State *mainState = lua_mainthread(L);
		if (L == mainState) {
			luaL_error(L, "Cannot Wait outside of a thread");
			return 0;
		}

		lua_pushthread(L);
		int threadReference = lua_ref(L, -1);
		lua_pop(L, 1);

		signal->Once(
			[L, mainState, threadReference, signal](CallbackArgument value) {
				int argumentCount = signal->LPushArgument(L, value);
				int status = lua_resume(L, mainState, argumentCount);
				if (status != LUA_OK && status != LUA_YIELD) {
					SDL_Log("Failed to resume thread after signal: %s", lua_tostring(L, -1));
					lua_pop(L, 1);
				};

				lua_unref(mainState, threadReference);
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
		auto argumentVector = std::make_shared<std::vector<int>>();
		argumentVector->reserve(argumentCount);

		for (int i = 2; i <= stackCount; ++i) {
			argumentVector->push_back(lua_ref(L, i));
		}

		signal->Fire(argumentVector);

		// Each argument was pinned in the registry to survive the trip out
		// through std::any and back onto whichever thread the handler runs on.
		// By the time Fire returns every handler has been resumed at least
		// once and LPushArgument has moved the values onto its stack, which
		// roots them, so the pins come off here. Left on, they are a GC root
		// per argument per firing, which a signal fired every frame never
		// stops growing.
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

		int pushedCount = 0;

		for (int ref : **argumentsPointer) {
			lua_getref(L, ref);
			pushedCount++;
		}

		return pushedCount;
	}

	int BaseSignal::LReferenceCallback(lua_State *L, int idx) {
		if (!lua_isfunction(L, idx)) {
			luaL_typeerror(L, idx, "function");
		}

		return lua_ref(L, idx);
	}

	// The handler gets a thread of its own rather than running on mainState:
	// mainState is what the ThreadEngine resumes from, and a handler that yields
	// on it cannot. The thread is referenced for the duration because the only
	// other thing rooting it is a stack slot the handler itself can move.
	void BaseSignal::LRunCallback(lua_State *mainState, BaseSignal *signal, int callbackReference, std::any value) {
		if (callbackReference == LUA_NOREF || callbackReference == LUA_REFNIL) {
			return;
		}

		int stackTop = lua_gettop(mainState);
		lua_State *thread = lua_newthread(mainState);

		int threadReference = lua_ref(mainState, -1);
		lua_pop(mainState, 1);

		lua_getref(mainState, callbackReference);
		if (!lua_isfunction(mainState, -1)) {
			lua_pop(mainState, 1);
			lua_unref(mainState, threadReference);
			return;
		}

		lua_xmove(mainState, thread, 1);

		// Labelled with whichever script the handler was written in, taken off
		// the function itself rather than asked for. A place with several
		// scripts connected to PreRender otherwise reports one lump of time
		// with nothing to say about which of them is spending it, and the
		// answer is already sitting in the function's debug info.
		std::string label;
		if (G_PROFILE_ACTIVE()) {
			lua_Debug info;
			if (lua_getinfo(thread, -1, "s", &info) && info.short_src) {
				label = info.short_src;

				// Luau reports a chunk loaded from a buffer as [string "Name"],
				// which is three quarters punctuation in a row that is already
				// short of width
				constexpr std::string_view WRAPPER = "[string \"";
				if (label.starts_with(WRAPPER) && label.ends_with("\"]")) {
					label = label.substr(WRAPPER.size(), label.size() - WRAPPER.size() - 2);
				}
			} else {
				label = "?";
			}
		}
		G_PROFILE_NAMED("Script", label.data(), label.size());

		int arguments = signal->LPushArgument(thread, value);
		int status = lua_resume(thread, mainState, arguments);

		if (status != LUA_OK && status != LUA_YIELD) {
			SDL_Log("Signal error: %s", lua_tostring(thread, -1));
		}

		lua_unref(mainState, threadReference);
		lua_settop(mainState, stackTop);
	}
} // namespace gargantuan
