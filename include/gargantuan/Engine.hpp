#pragma once

#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/ecs/Scheduler.hpp"
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

		static constexpr float MAXIMUM_DELTA_SECONDS = 0.1f;

		float GetDeltaTime() {
			float delta = (float)((double)(CurrentTick - LastTick) / 1000000000.0);
			return delta > MAXIMUM_DELTA_SECONDS ? MAXIMUM_DELTA_SECONDS : delta;
		};
		void ProcessEvent(SDL_Event event);
		void Step();

		ecs::Scheduler Scheduler;

		void ProfileAndExit(double seconds);

		int64_t MaximumFrames = -1;
		uint64_t FramesRun = 0;

		void SetDebugPanels(bool statistics, bool systemTimings) {
			ShowStatistics = statistics;
			ShowSystemTimings = systemTimings;
		}

		void SetProfilerTab(ProfilerTab tab) {
			Profiler.Tab = tab;
			Profiler.Scroll = 0;
		}

	  private:
		uint64_t CurrentTick = 0;
		uint64_t LastTick = 0;

		FrameStatistics Statistics;
		bool ShowStatistics = false;
		std::shared_ptr<EditableImage> StatisticsPanel;
		static constexpr double STATISTICS_REFRESH_SECONDS = 0.2;
		double LastStatisticsRefresh = 0.0;
		static constexpr float STATISTICS_MARGIN = 8.0f;

		void UpdateStatistics(double now, float deltaTime);

		static constexpr double SETTLE_AFTER_RESUME = 0.3;
		double SettleUntil = 0.0;

		double AutomaticProfileSeconds = -1.0;
		double AutomaticProfileStarted = 0.0;
		void UpdateAutomaticProfile(double now);

		void RegisterSystems();
		void StepEvents(float deltaTime, double seconds);
		void StepStatistics(float deltaTime, double seconds);
		void StepSimulation(float deltaTime);
		void StepGpuWait();
		void StepRender(float deltaTime, double seconds);
		void StepScripts();

		double FrameSeconds = 0.0;
		std::vector<double> SystemTotals;
		uint64_t ProfiledFrames = 0;
		void ReportSystemTotals(double elapsed) const;
		bool ShowSystemTimings = false;
		OverlayImage OverlayBuffer;
		ProfilerView Profiler;

		void BuildProfilerView();
		void WriteProfilerSnapshot();

		int AppliedBufferCount = 0;
		void ApplyBufferCount();

		void PaceSimulation();
		std::vector<ProfilerCategory> ProfilerZoneCategories;
		std::vector<uint32_t> ProfilerZoneRoots;
		std::vector<uint8_t> ProfilerZoneHasChildren;
		std::vector<uint8_t> ProfilerZoneVisible;
		std::vector<float> ProfilerZoneChildTotals;

		void BuildCounterRows();
		void CountWorld();

		bool StepProfilerKey(const SDL_Event &event);
	};
}
