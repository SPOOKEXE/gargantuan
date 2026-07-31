#include "gargantuan/ecs/Scheduler.hpp"

#include "gargantuan/Profiler.hpp"

#include <SDL3/SDL_timer.h>
#include <utility>

namespace gargantuan::ecs {
	namespace {
		// Cache the process-invariant frequency used twice per system per frame.
		const uint64_t Frequency = SDL_GetPerformanceFrequency();
	}

	std::string_view GetPhaseName(Phase phase) {
		switch (phase) {
		case Phase::OnLoad: return "OnLoad";
		case Phase::PostLoad: return "PostLoad";
		case Phase::PreUpdate: return "PreUpdate";
		case Phase::OnUpdate: return "OnUpdate";
		case Phase::OnValidate: return "OnValidate";
		case Phase::PostUpdate: return "PostUpdate";
		case Phase::PreStore: return "PreStore";
		case Phase::OnStore: return "OnStore";
		default: return "Unknown";
		}
	}

	void Scheduler::Add(Phase phase, std::string_view name, std::function<void(float)> run) {
		Systems[(size_t)phase].push_back({name, std::move(run)});
	}

	void Scheduler::Run(float deltaTime) {
		for (size_t index = 0; index < Systems.size(); index++) {
			auto &systems = Systems[index];
			if (systems.empty()) continue;

			std::string_view phaseName = GetPhaseName((Phase)index);
			// Both names have static lifetime.
			G_PROFILE_NAMED_STABLE("Phase", phaseName);

			for (auto &system : systems) {
				G_PROFILE_NAMED_STABLE("System", system.Name);

				uint64_t started = SDL_GetPerformanceCounter();
				system.Run(deltaTime);
				system.LastMilliseconds =
					(float)((double)(SDL_GetPerformanceCounter() - started) * 1000.0 / (double)Frequency);
			}
		}
	}
}
