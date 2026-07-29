#pragma once

#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/DebugOverlay.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/services/RenderSettings.hpp"
#include "gargantuan/services/RunService.hpp"
#include "gargantuan/services/TweenService.hpp"
#include "gargantuan/services/UserInputService.hpp"
#include "gargantuan/services/Workspace.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/gtc/matrix_transform.hpp>
#include <lua.h>
#include <memory>

#include "gargantuan/math/LerpValue.hpp"

namespace gargantuan {
	class Engine {
	  public:
		bool IsRunning = true;
		glm::vec2 ViewportSize = glm::vec2(720, 540);
		std::shared_ptr<DataModel> DataModel = nullptr;
		std::shared_ptr<Workspace> Workspace = nullptr;
		std::shared_ptr<Lighting> Lighting = nullptr;
		std::shared_ptr<RunService> RunService = nullptr;
		std::shared_ptr<TweenService> TweenService = nullptr;
		std::shared_ptr<UserInputService> UserInputService = nullptr;
		std::shared_ptr<RenderSettings> RenderSettings = nullptr;

		SDL_Window *Window;
		SDL_GPUDevice *Gpu;
		RenderProvider *RenderProvider;
		ScriptEngine *ScriptEngine;

		Engine();
		~Engine();

		// Nanoseconds, so the step is not quantised to a whole millisecond.
		// It used to be: at a high frame rate most frames measured zero and
		// DistributedGameTime advanced in 1 ms jumps, which rounded every
		// camera interval up to the next millisecond -- asking for 240 fps got
		// 200, because 4.17 ms became 5.
		float GetDeltaTime() {
			return (float)((double)(CurrentTick - LastTick) / 1000000000.0);
		};
		void ProcessEvent(SDL_Event event);
		void Step();

	  private:
		uint64_t CurrentTick = 0;
		uint64_t LastTick = 0;

		// F3 shows the frame rate, the way it does in every other engine that
		// has one. Measured whether or not it is on screen, so turning it on
		// during a stutter shows what just happened rather than starting a
		// twenty second wait for the window to fill.
		FrameStatistics Statistics;
		bool ShowStatistics = false;
		std::shared_ptr<EditableImage> StatisticsPanel;
		// Redrawn a few times a second rather than every frame. The numbers are
		// unreadable changing sixty times a second, and repainting the panel is
		// work the thing being measured would be charged for.
		static constexpr double STATISTICS_REFRESH_SECONDS = 0.2;
		double LastStatisticsRefresh = 0.0;
		// Where it sits, in pixels from the window's top left
		static constexpr float STATISTICS_MARGIN = 8.0f;

		void UpdateStatistics(double now, float deltaTime);
	};
} // namespace gargantuan
