#include "gargantuan/Engine.hpp"
#include "gargantuan/Profiler.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/datatypes/Enum.hpp"
#include "gargantuan/reflection/TypedefGenerator.hpp"

#include "gargantuan/math/LerpValue.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
// Android loads SDL_main from libmain.so; this wrapper provides that entry point.
#include <SDL3/SDL_main.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
	// Must run before window and GPU initialization.
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--typedefs") != 0) {
			continue;
		}

		const char *path = (i + 1 < argc) ? argv[i + 1] : "gargantuan.d.luau";
		if (!gargantuan::TypedefGenerator::WriteToFile(path)) {
			SDL_Log("Failed to write type definitions to %s", path);
			return 1;
		}

		SDL_Log("Wrote type definitions to %s", path);
		return 0;
	}

	SDL_Init(SDL_INIT_VIDEO);
	std::atexit(SDL_Quit);

	gargantuan::LerpValue<float>::Lerp(0, 1, 0.5);

	SDL_Log("enums:");
	for (auto &it : gargantuan::Enums::GetEnums()) {
		SDL_Log("enum %s", it.first.data());
	};
	SDL_Log("end enums");

	// Must precede swapchain creation.
	bool uncapped = false;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--uncapped") == 0) {
			uncapped = true;
		}
	}

	int64_t frameBudget = -1;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--frames") != 0) {
			continue;
		}
		if (i + 1 < argc) {
			char *end = nullptr;
			long long parsed = std::strtoll(argv[i + 1], &end, 10);
			if (end && end != argv[i + 1] && parsed > 0) {
				frameBudget = parsed;
			}
		}
	}

	// ScriptEngine reads this during Engine construction.
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--script") != 0) {
			continue;
		}
		if (i + 1 < argc) {
			gargantuan::ScriptEngine::StartupScriptPath = argv[i + 1];
		} else {
			SDL_Log("--script needs a path");
		}
		break;
	}

	bool showPanels = false;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--stats") == 0) {
			showPanels = true;
		}
	}

	gargantuan::Engine engine;
	engine.MaximumFrames = frameBudget;
	if (showPanels) {
		engine.SetDebugPanels(true, true);
	}

	for (int i = 1; i + 1 < argc; i++) {
		if (std::strcmp(argv[i], "--profiler-tab") != 0) {
			continue;
		}

		bool matched = false;
		for (size_t tab = 0; tab < (size_t)gargantuan::ProfilerTab::Count; tab++) {
			auto candidate = (gargantuan::ProfilerTab)tab;
			std::string name(gargantuan::GetProfilerTabName(candidate));
			for (char &character : name) {
				character = (char)std::tolower((unsigned char)character);
			}

			if (name == argv[i + 1]) {
				engine.SetProfilerTab(candidate);
				engine.SetDebugPanels(showPanels, true);
				matched = true;
				break;
			}
		}
		if (!matched) {
			SDL_Log("--profiler-tab: no tab called %s", argv[i + 1]);
		}
		break;
	}
	if (uncapped) {
		engine.RenderProvider->ShouldPresentUncapped = true;
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(engine.Window, &width, &height);
		engine.RenderProvider->Resize(width, height);
		SDL_Log("Presenting uncapped");
	}

	// TRACY_ON_DEMAND collects nothing before attachment; wait to avoid empty short runs.
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--auto-enable-profiler") != 0) {
			continue;
		}

		double timeout = 10.0;
		if (i + 1 < argc) {
			char *end = nullptr;
			double parsed = std::strtod(argv[i + 1], &end);
			if (end && end != argv[i + 1] && parsed > 0.0) {
				timeout = parsed;
			}
		}

		if (!G_PROFILE_ACTIVE()) {
			SDL_Log("Waiting up to %.1f s for a Tracy profiler to connect", timeout);
			uint64_t started = SDL_GetTicksNS();
			while (!G_PROFILE_ACTIVE()) {
				if ((double)(SDL_GetTicksNS() - started) / 1000000000.0 >= timeout) {
					SDL_Log("No profiler connected; continuing without one");
					break;
				}
				SDL_Delay(50);
			}
		}
		if (G_PROFILE_ACTIVE()) {
			SDL_Log("Profiler attached");
		}
		break;
	}

	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--profile") != 0 && std::strcmp(argv[i], "--profile-seconds") != 0) {
			continue;
		}

		double seconds = 6.0;
		if (i + 1 < argc) {
			char *end = nullptr;
			double parsed = std::strtod(argv[i + 1], &end);
			if (end && end != argv[i + 1] && parsed > 0.0) {
				seconds = parsed;
			}
		}

		SDL_Log("Profiling for %.1f s", seconds);
		engine.ProfileAndExit(seconds);
		break;
	}

	while (engine.IsRunning) {
		engine.Step();
	}

	return 0;
}
