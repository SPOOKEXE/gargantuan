#include "gargantuan/scripting/ThreadEngine.hpp"

#include "gargantuan/Profiler.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <lua.h>
#include <stdexcept>

namespace gargantuan {
	// The same clock the frame is timed on. It used to be steady_clock, which
	// meant task.wait and the engine's own delta were measured against two
	// different clocks that need not agree with each other.
	double GetCurrentTime() {
		return (double)SDL_GetTicksNS() / 1000000000.0;
	};

	ThreadEngine::ThreadEngine(lua_State *mainState) : L(mainState) {
		lua_pushstring(L, "gargantuan::ThreadEngine");
		lua_pushlightuserdata(L, this);
		lua_settable(L, LUA_REGISTRYINDEX);
	};

	ThreadEngine *ThreadEngine::Get(lua_State *L) {
		lua_pushstring(L, "gargantuan::ThreadEngine");
		lua_gettable(L, LUA_REGISTRYINDEX);

		auto *engine = static_cast<ThreadEngine *>(lua_tolightuserdata(L, -1));
		lua_pop(L, 1);

		if (!engine) {
			throw std::runtime_error("Missing gargantuan::ThreadEngine");
		}

		return engine;
	}

	int ThreadEngine::TakeThreadReference(lua_State *thread) {
		lua_pushthread(thread);
		lua_xmove(thread, L, 1);
		int reference = lua_ref(L, -1);
		// lua_ref keeps the value on the stack
		lua_pop(L, 1);
		return reference;
	}

	void ThreadEngine::ResumeThread(lua_State *thread, int threadReference, int argumentCount) {
		int status = lua_resume(thread, L, argumentCount);
		if (status != LUA_OK && status != LUA_YIELD) {
			SDL_Log("Thread error: %s", lua_tostring(thread, -1));
		}
		lua_unref(L, threadReference);
	}

	void ThreadEngine::Step() {
		// The two queues separately: a frame spent resuming task.wait sleepers
		// and one spent draining task.defer are different problems, and one
		// number covering both says which frame but not which queue.
		G_PROFILE("luau.threads");

		auto currentTime = GetCurrentTime();
		{
			G_PROFILE("luau.scheduled");
			while (!ScheduledQueue.empty() && ScheduledQueue.top().WakeTime <= currentTime) {
				auto task = ScheduledQueue.top();
				ScheduledQueue.pop();

				switch (task.type) {
				case ThreadEngine::ScheduledTask::Type::Delay: {
					ResumeThread(task.Thread, task.ThreadReference, task.ArgumentCount);
					break;
				}
				case ThreadEngine::ScheduledTask::Type::Wait: {
					double actualWait = currentTime - task.ScheduledTime;
					lua_pushnumber(task.Thread, actualWait);
					ResumeThread(task.Thread, task.ThreadReference, 1);
					break;
				}
				}
			}
		}

		{
			G_PROFILE("luau.deferred");
			while (!DeferredQueue.empty()) {
				std::vector<DeferredTask> currentBatch;
				currentBatch.swap(DeferredQueue);

				for (auto &task : currentBatch) {
					ResumeThread(task.Thread, task.ThreadReference, task.ArgumentCount);
				}
			}
		}
	}

	void ThreadEngine::QueueScheduledTask(
		lua_State *thread, ScheduledTask::Type type, double delaySeconds, int argumentCount
	) {
		ScheduledQueue.push({
			.type = type,
			.Thread = thread,
			.ThreadReference = TakeThreadReference(thread),
			.ArgumentCount = argumentCount,
			.ScheduledTime = GetCurrentTime(),
			.WakeTime = GetCurrentTime() + delaySeconds,
		});
	}

	void ThreadEngine::QueueDeferredTask(lua_State *thread, int argumentCount) {
		DeferredQueue.push_back({
			.Thread = thread,
			.ThreadReference = TakeThreadReference(thread),
			.ArgumentCount = argumentCount,
		});
	}
} // namespace gargantuan
