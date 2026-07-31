#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/datatypes/Enum.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/datatypes/Axes.hpp"
#include "gargantuan/datatypes/Random.hpp"
#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/scripting/ThreadEngine.hpp"

#include <Luau/Common.h>
#include <Luau/Compiler.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <filesystem>
#include <stdexcept>

namespace gargantuan {
	// https://youtu.be/hP0NCTU81A4?si=aE-SV_ifAW745_M8
	void DumpLuaStack(lua_State *L) {
		int stackSize = lua_gettop(L);

		printf("Lua Stack Contents:\n");
		printf("Stack Size: %d\n", stackSize);

		for (int i = stackSize; i >= 1; --i) {
			int type = lua_type(L, i);
			printf("[%d] ", i);

			switch (type) {
			case LUA_TNIL:
				printf("nil\n");
				break;
			case LUA_TBOOLEAN:
				printf(lua_toboolean(L, i) ? "true\n" : "false\n");
				break;
			case LUA_TNUMBER:
				printf("%g\n", lua_tonumber(L, i));
				break;
			case LUA_TSTRING:
				printf("%s\n", lua_tostring(L, i));
				break;
			case LUA_TTABLE:
				printf("table\n");
				break;
			case LUA_TFUNCTION:
				printf("function\n");
				break;
			case LUA_TUSERDATA:
				printf("userdata\n");
				break;
			case LUA_TTHREAD:
				printf("thread\n");
				break;
			case LUA_TLIGHTUSERDATA:
				printf("lightuserdata\n");
				break;
			default:
				printf("unknown\n");
				break;
			}
		}

		printf("----------\n");
	}

	static const luaL_Reg SCRIPT_LIBS[] = {
		{"", OpenLibBase},

		{"CFrame", OpenLibCFrame},
		{"Color3", OpenLibColor3},
		{"Color4", OpenLibColor4},
		{"Enum", OpenLibEnum},
		{"Instance", OpenLibInstance},
		{"PhysicalProperties", OpenLibPhysicalProperties},
		{"Axes", OpenLibAxes},
		{"Random", OpenLibRandom},
		{"UDim", OpenLibUDim},
		{"UDim2", OpenLibUDim2},
		{"Signal", OpenLibSignal},
		{"Vector2", OpenLibVector2},
		{"Vector3", OpenLibVector3},
		{"TweenInfo", OpenLibTweenInfo},

		{nullptr, nullptr},
	};

	static int LuauAssertHandler(const char *expression, const char *file, int line, const char *function) {
		SDL_Log("Luau assertion failed:\n\tExpression: %s\n\tIn: %s:%d in %s", expression, file, line, function);
		assert(false);
		// assert compiles away with NDEBUG, so the handler has to return on its
		// own. Non-zero asks Luau to break into the debugger and abort.
		return 1;
	}

	ScriptEngine::ScriptEngine() : L(luaL_newstate()), ThreadEngine(L) {
		if (L == nullptr) {
			throw std::runtime_error("Failed to instantiate Luau VM");
		}

		Luau::assertHandler() = LuauAssertHandler;

		luaL_openlibs(L);
		OpenLibTask(L, &ThreadEngine);

		BaseSignal::CreateUserdataMetatable(L);
		CFrame::CreateUserdataMetatable(L);
		Color3::CreateUserdataMetatable(L);
		Color4::CreateUserdataMetatable(L);
		Enum::CreateUserdataMetatable(L);
		EnumItem::CreateUserdataMetatable(L);
		Instance::CreateUserdataMetatable(L);
		PhysicalProperties::CreateUserdataMetatable(L);
		Axes::CreateUserdataMetatable(L);
		Random::CreateUserdataMetatable(L);
		SignalConnection::CreateUserdataMetatable(L);
		TweenInfo::CreateUserdataMetatable(L);
		UDim::CreateUserdataMetatable(L);
		UDim2::CreateUserdataMetatable(L);
		Vector2::CreateUserdataMetatable(L);

		const luaL_Reg *lib = SCRIPT_LIBS;
		for (; lib->func; lib++) {
			lua_pushcfunction(L, lib->func, nullptr);
			lua_pushstring(L, lib->name);
			lua_call(L, 1, 0);
		}

		CreateTestbedThread();
	}

	std::string ScriptEngine::StartupScriptPath = "Testbed.luau";

	void ScriptEngine::CreateTestbedThread() {
		testbedThread = lua_newthread(L);
		size_t fileSize;
		const std::string &path = StartupScriptPath;
		void *code = SDL_LoadFile(path.c_str(), &fileSize);

		if (code == nullptr) {
			SDL_Log("Failed to load %s", path.c_str());
			return;
		}
		SDL_Log("Running %s", path.c_str());

		std::string contents((char *)code, fileSize);
		SDL_free(code);

		size_t bytecodeSize;
		char *bytecode = luau_compile(contents.c_str(), contents.length(), nullptr, &bytecodeSize);

		std::string chunkName = std::filesystem::path(path).stem().string();
		luau_load(testbedThread, chunkName.c_str(), bytecode, bytecodeSize, 0);
		std::free(bytecode);

		ThreadEngine.QueueDeferredTask(testbedThread, 0);
	}

	ScriptEngine::~ScriptEngine() {
		if (L) {
			lua_close(L);
			L = nullptr;
		}
	}

	void ScriptEngine::Step() {
		ThreadEngine.Step();
	}
} // namespace gargantuan
