#include "gargantuan/Engine.hpp"
#include "gargantuan/datatypes/Enum.hpp"

#include "gargantuan/math/LerpValue.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
// Renames main to SDL_main and supplies the platform's real entry point around
// it. Desktop hardly notices; Android has no entry point of its own, and the
// Java activity dlsym()s SDL_main out of libmain.so, so without this the
// Android build links but cannot start.
#include <SDL3/SDL_main.h>
#include <cstdlib>
#include <spdlog/spdlog.h>

int main() {
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
