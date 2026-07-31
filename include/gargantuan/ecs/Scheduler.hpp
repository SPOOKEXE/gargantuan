#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace gargantuan::ecs {
	// Phases, borrowed wholesale from flecs' default pipeline. Systems are
	// ordered by phase first and declaration order second, which is deliberately
	// rigid: it stops one system from naming another as a dependency, so a
	// module can be swapped out without rewiring the ones around it.
	enum class Phase : uint8_t {
		// Pull in external state: window events, keyboard, mouse.
		OnLoad,
		// Turn that raw state into something the rest of the frame can use.
		PostLoad,
		// Clear last frame's leftovers.
		PreUpdate,
		// Gameplay. The default.
		OnUpdate,
		// Check the world after the update: collision, constraints.
		OnValidate,
		// Apply whatever validation decided.
		PostUpdate,
		// Prepare to draw: refresh cached rows, build matrices.
		PreStore,
		// Draw.
		OnStore,

		Count,
	};

	std::string_view GetPhaseName(Phase phase);

	// One system: a name, a phase, and a function. Keep each to a single
	// responsibility -- small systems are easier to isolate when something goes
	// wrong, and give the compiler a straight loop to vectorise.
	struct System {
		std::string_view Name;
		std::function<void(float)> Run;

		// What the last frame cost this system. Two clock reads per system per
		// frame, which at this many systems is beneath measuring -- and the F5
		// panel is worthless if it only measures while it is open, because the
		// frame it is opened on is the one that went wrong.
		float LastMilliseconds = 0.0f;
	};

	class Scheduler {
	  public:
		void Add(Phase phase, std::string_view name, std::function<void(float)> run);
		void Run(float deltaTime);

		const std::vector<System> &GetSystems(Phase phase) const {
			return Systems[(size_t)phase];
		}

	  private:
		std::array<std::vector<System>, (size_t)Phase::Count> Systems;
	};
} // namespace gargantuan::ecs
