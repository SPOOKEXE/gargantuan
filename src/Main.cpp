#include "gargantuan/Engine.hpp"
#include "gargantuan/datatypes/Enum.hpp"
#include "gargantuan/reflection/TypedefGenerator.hpp"

#include "gargantuan/math/LerpValue.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
	// Emitting type definitions needs neither a window nor a GPU, so it is
	// handled before anything is initialised
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

	gargantuan::Engine engine;
	while (engine.IsRunning) {
		engine.Step();
	}

	return 0;
}
