#pragma once

#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/DebugOverlay.hpp"
#include "gargantuan/Profiler.hpp"
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
		// A frame spanning a stall is not one anybody rendered. Unclamped, a
		// return from an unfocused window hands the whole gap to the simulation
		// and everything teleports.
		static constexpr float MAXIMUM_DELTA_SECONDS = 0.1f;

		float GetDeltaTime() {
			float delta = (float)((double)(CurrentTick - LastTick) / 1000000000.0);
			return delta > MAXIMUM_DELTA_SECONDS ? MAXIMUM_DELTA_SECONDS : delta;
		};
		void ProcessEvent(SDL_Event event);
		void Step();

		// Public because Profiler::GetCurrent() points at this one
		class Profiler Profiler;

		// Runs the chart, writes the report after `seconds`, then stops. Two
		// builds cannot be compared by a person pressing F6 at the right moment.
		void ProfileAndExit(double seconds);

	  private:
		uint64_t CurrentTick = 0;
		uint64_t LastTick = 0;

		// Measured whether or not it is on screen, so turning it on during a
		// stutter shows what just happened
		FrameStatistics Statistics;
		bool ShowStatistics = false;
		std::shared_ptr<EditableImage> StatisticsPanel;
		// The numbers are unreadable changing sixty times a second, and
		// repainting is work the thing being measured would be charged for
		static constexpr double STATISTICS_REFRESH_SECONDS = 0.2;
		double LastStatisticsRefresh = 0.0;
		// Where it sits, in pixels from the window's top left
		static constexpr float STATISTICS_MARGIN = 8.0f;

		void UpdateStatistics(double now, float deltaTime);

		// Frames are not counted until this. A settle rather than one frame:
		// the compositor stops presenting while the window is away and then
		// lets a burst through, which reads as a minimum of five and a maximum
		// of a thousand and then sits in the counter for its whole window.
		static constexpr double SETTLE_AFTER_RESUME = 0.3;
		double SettleUntil = 0.0;

		// Apart from the F3 counter: that is two numbers a frame, this walks a
		// tree
		bool ShowProfiler = false;
		std::shared_ptr<EditableImage> ProfilerPanel;
		ProfilerPanelLayout ProfilerLayout;
		// Sits under the frame rate counter, which is why it is not at the top
		static constexpr float PROFILER_TOP = 62.0f;
		double LastProfilerRefresh = 0.0;
		// What the export last did, shown on the panel until the next one
		std::string ProfilerStatus;

		void UpdateProfiler(double now);
		// Negative when nothing asked for an unattended run
		double AutomaticProfileSeconds = -1.0;
		double AutomaticProfileStarted = 0.0;
		// True when the click landed on the export button
		bool HandleProfilerClick(float x, float y);
		void ExportProfilerReport();

		// Wrapped so the whole frame is one zone without straddling a return
		void StepFrame(float deltaTime, double seconds);
	};
} // namespace gargantuan
