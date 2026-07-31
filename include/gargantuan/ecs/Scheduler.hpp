#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace gargantuan::ecs {
	enum class Phase : uint8_t {
		OnLoad,
		PostLoad,
		PreUpdate,
		OnUpdate,
		OnValidate,
		PostUpdate,
		PreStore,
		OnStore,

		Count,
	};

	std::string_view GetPhaseName(Phase phase);

	struct System {
		std::string_view Name;
		std::function<void(float)> Run;

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
}
