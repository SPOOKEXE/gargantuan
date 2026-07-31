#pragma once

#include "gargantuan/DebugOverlay.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/ecs/Scheduler.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/services/RunService.hpp"
#include "gargantuan/services/TweenService.hpp"
#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/services/Workspace.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/gtc/matrix_transform.hpp>
#include <lua.h>
#include <memory>
#include <vector>

#include "gargantuan/math/LerpValue.hpp"

namespace gargantuan {
	class Engine {
	  public:
		bool IsRunning = true;
		glm::vec2 ViewportSize = glm::vec2(720, 540);
		// Qualified so the member names do not change the meaning of the class
		// names in this scope (-Wchanges-meaning).
		std::shared_ptr<gargantuan::DataModel> DataModel = nullptr;
		std::shared_ptr<gargantuan::Workspace> Workspace = nullptr;
		std::shared_ptr<gargantuan::Lighting> Lighting = nullptr;
		std::shared_ptr<gargantuan::RunService> RunService = nullptr;
		std::shared_ptr<gargantuan::UserInputService> UserInputService = nullptr;

		SDL_Window *Window;
		SDL_GPUDevice *Gpu;
		gargantuan::RenderProvider *RenderProvider;
		gargantuan::ScriptEngine *ScriptEngine;

		ecs::Scheduler Scheduler;

		Engine();
		~Engine();

		float GetDeltaTime() {
			return (CurrentTick - LastTick) / 1000.0f;
		};
		void ProcessEvent(SDL_Event event);
		void Step();

	  private:
		void RegisterSystems();

		// F3 is the frame rate, F5 the per-system breakdown. Both draw into one
		// image because they stack into a single panel.
		void UpdateOverlay(double now, float deltaTime);

		bool ShowStatistics = false;
		bool ShowSystems = false;
		FrameStatistics Statistics;
		OverlayImage Overlay;
		std::vector<SystemTiming> SystemTimings;

		uint64_t CurrentTick = 0;
		uint64_t LastTick = 0;
	};
} // namespace gargantuan
