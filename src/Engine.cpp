#include "gargantuan/Engine.hpp"
#include "gargantuan/ProfilerExport.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/services/Workspace.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <cstdlib>
#include <cstring>
#include <fwd.hpp>
#include <glm/glm.hpp>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <memory>
#include <vector>
#include <stdexcept>

namespace gargantuan {
	Engine::Engine() {
		this->Gpu = SDL_CreateGPUDevice(
			SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_MSL, true, nullptr
		);
		if (!Gpu) {
			throw std::runtime_error("Failed to instantiate GPU");
		}

		this->Window = SDL_CreateWindow(
			"Gargantuan",
			ViewportSize.x,
			ViewportSize.y,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY
		);
		if (!Window) {
			throw std::runtime_error("Failed to instantiate window");
		}

		this->RenderProvider = new class RenderProvider(Window, Gpu);
		// Camera:Render() reaches the renderer through this
		RenderProvider::SetCurrent(this->RenderProvider);
		// And the renderer's passes, and Luau callbacks, reach the profiler
		Profiler::SetCurrent(&this->Profiler);

		this->ScriptEngine = new class ScriptEngine();

		DataModel = std::make_shared<gargantuan::DataModel>();
		DataModel->Name = "Welcome To Hell";

		auto workspace = this->DataModel->GetService("Workspace");
		this->Workspace = std::dynamic_pointer_cast<gargantuan::Workspace>(workspace);

		auto lighting = this->DataModel->GetService("Lighting");
		this->Lighting = std::dynamic_pointer_cast<gargantuan::Lighting>(lighting);

		auto runService = this->DataModel->GetService("RunService");
		this->RunService = std::dynamic_pointer_cast<gargantuan::RunService>(runService);

		auto uis = this->DataModel->GetService("UserInputService");
		this->UserInputService = std::dynamic_pointer_cast<gargantuan::UserInputService>(uis);

		auto tweenService = this->DataModel->GetService("TweenService");
		this->TweenService = std::dynamic_pointer_cast<gargantuan::TweenService>(tweenService);

		auto renderSettings = this->DataModel->GetService("RenderSettings");
		this->RenderSettings = std::dynamic_pointer_cast<gargantuan::RenderSettings>(renderSettings);

		StackValue<Instance::Pointer>::Push(ScriptEngine->L, this->DataModel);
		lua_pushvalue(ScriptEngine->L, -1);
		lua_setglobal(ScriptEngine->L, "game");
	}

	Engine::~Engine() {
		SDL_Log("destroying window");
		SDL_ReleaseWindowFromGPUDevice(Gpu, Window);
		SDL_DestroyWindow(Window);

		SDL_Log("destroying mesh provider");
		MeshProvider::Destroy(Gpu);

		RenderProvider->Destroy();

		SDL_Log("destroying gpu %s", Gpu ? "exists" : "not exist");
		SDL_DestroyGPUDevice(Gpu);
		Gpu = nullptr;
		SDL_Log("done destroying gpu");
	}

	void Engine::ProcessEvent(SDL_Event event) {
		switch (event.type) {
		case SDL_EVENT_QUIT:
			SDL_Log("Stopping");
			IsRunning = false;
			return;
		}
	}

	void Engine::ProfileAndExit(double seconds) {
		AutomaticProfileSeconds = seconds;
		AutomaticProfileStarted = 0.0;
		ShowProfiler = true;
	}

	void Engine::UpdateProfiler(double now) {
		Profiler.SetEnabled(ShowProfiler);

		// An unattended run: gather for as long as it was asked to, write the
		// report and stop. The first frame is where the clock starts, not
		// construction, so the window is time actually spent rendering.
		if (AutomaticProfileSeconds >= 0.0) {
			if (AutomaticProfileStarted == 0.0) {
				AutomaticProfileStarted = now;
			} else if (now - AutomaticProfileStarted >= AutomaticProfileSeconds) {
				ExportProfilerReport();
				SDL_Log("Profiled for %.1f s, stopping", now - AutomaticProfileStarted);
				AutomaticProfileSeconds = -1.0;
				IsRunning = false;
				return;
			}
		}

		if (!ShowProfiler) {
			return;
		}

		if (!ProfilerPanel) {
			ProfilerPanel = std::make_shared<EditableImage>();
			ProfilerPanel->Name = EditableImage::DEFINITION.Name;
			LastProfilerRefresh = 0.0;
		}

		// Only when a new second has been published. Redrawing the same numbers
		// in between would be work charged to the frame that is being measured,
		// which is the one thing a profiler must not do.
		if (!Profiler.HasSnapshot()) {
			if (LastProfilerRefresh == 0.0) {
				ProfilerLayout = DrawProfilerPanel(*ProfilerPanel, Profiler::Snapshot{}, "GATHERING");
				LastProfilerRefresh = now;
			}
		} else if (now - LastProfilerRefresh >= Profiler::WINDOW_SECONDS * 0.5) {
			ProfilerLayout = DrawProfilerPanel(*ProfilerPanel, Profiler.Latest(), ProfilerStatus);
			LastProfilerRefresh = now;
		}
	}

	bool Engine::HandleProfilerClick(float x, float y) {
		if (!ShowProfiler || !ProfilerPanel) {
			return false;
		}

		// SDL reports the pointer in window coordinates while the panel is
		// placed in swapchain pixels, and on a display that scales those are
		// not the same number. The window is created asking for high density,
		// so this is the ordinary case rather than the exotic one.
		int windowWidth = 0, windowHeight = 0;
		int pixelWidth = 0, pixelHeight = 0;
		SDL_GetWindowSize(Window, &windowWidth, &windowHeight);
		SDL_GetWindowSizeInPixels(Window, &pixelWidth, &pixelHeight);
		if (windowWidth > 0 && windowHeight > 0) {
			x *= (float)pixelWidth / (float)windowWidth;
			y *= (float)pixelHeight / (float)windowHeight;
		}

		// Both offsets, because the panel is inset from the left as well as
		// pushed down under the frame rate counter
		float left = STATISTICS_MARGIN + ProfilerLayout.ButtonPosition.GetX();
		float top = PROFILER_TOP + ProfilerLayout.ButtonPosition.GetY();
		float right = left + ProfilerLayout.ButtonSize.GetX();
		float bottom = top + ProfilerLayout.ButtonSize.GetY();

		if (x < left || x > right || y < top || y > bottom) {
			return false;
		}

		ExportProfilerReport();
		return true;
	}

	void Engine::ExportProfilerReport() {
		std::string report;
		if (ExportProfile(Profiler.Latest(), "profiles", report)) {
			SDL_Log("Wrote %s", report.c_str());
			ProfilerStatus = "WROTE PROFILES";
		} else {
			SDL_Log("Could not export a profile: %s", report.c_str());
			ProfilerStatus = "EXPORT FAILED";
		}

		// Repainted at once so the answer is on screen before the next window
		// comes round, rather than up to half a second later
		if (ProfilerPanel) {
			ProfilerLayout = DrawProfilerPanel(*ProfilerPanel, Profiler.Latest(), ProfilerStatus);
		}
	}

	void Engine::UpdateStatistics(double now, float deltaTime) {
		// Recorded even while hidden. The counter is turned on because
		// something went wrong a moment ago, and a window that only starts
		// filling at that point has nothing to say about it for twenty seconds.
		//
		// Except across a resume, where the frame is an artefact of the window
		// having been away rather than anything the engine did. Letting it in
		// pins the maximum at whatever the catch-up burst managed and the
		// minimum at the length of the pause, and both stay there for the whole
		// twenty second window.
		if (now >= SettleUntil) {
			Statistics.Record(now, deltaTime);
		}

		if (!ShowStatistics) {
			// Handing over nothing is what takes it off the window; the panel
			// itself is kept, so turning it back on does not have to build one
			RenderProvider->SetWindowOverlay(0, nullptr, glm::vec2(0.0f));
			return;
		}

		if (!StatisticsPanel) {
			StatisticsPanel = std::make_shared<EditableImage>();
			StatisticsPanel->Name = EditableImage::DEFINITION.Name;
			// Nothing to draw from yet, so paint it now rather than leaving a
			// blank rectangle on screen until the first refresh comes round
			LastStatisticsRefresh = 0.0;
		}

		if (LastStatisticsRefresh == 0.0 || now - LastStatisticsRefresh >= STATISTICS_REFRESH_SECONDS) {
			DrawStatisticsPanel(*StatisticsPanel, Statistics);
			LastStatisticsRefresh = now;
		}

		RenderProvider->SetWindowOverlay(
			0, StatisticsPanel, glm::vec2(STATISTICS_MARGIN, STATISTICS_MARGIN)
		);
	}

	void Engine::Step() {
		if (!IsRunning) {
			return;
		}

		CurrentTick = SDL_GetTicksNS();
		if (LastTick == 0) {
			LastTick = CurrentTick;
		}
		float deltaTime = GetDeltaTime();

		double seconds = (double)CurrentTick / 1000000000.0;
		Profiler.BeginFrame(seconds);

		{
			G_PROFILE("Main Thread");
			StepFrame(deltaTime, seconds);
		}

		Profiler.EndFrame(seconds);
	}

	void Engine::StepFrame(float deltaTime, double seconds) {
		SDL_Event event;
		{
		G_PROFILE("Events");
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				IsRunning = false;
				return;
			}

			// The window coming back is not a frame anyone saw. Whatever the
			// compositor did while it was away, the timings across the gap
			// belong to the gap and not to the engine.
			if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_RESTORED ||
				event.type == SDL_EVENT_WINDOW_SHOWN || event.type == SDL_EVENT_WINDOW_EXPOSED) {
				SettleUntil = seconds + SETTLE_AFTER_RESUME;
			}

			// Held down, these would repeat and flicker the panels on and off
			if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				if (event.key.key == SDLK_F3) {
					ShowStatistics = !ShowStatistics;
				} else if (event.key.key == SDLK_F6) {
					ShowProfiler = !ShowProfiler;
					ProfilerStatus.clear();
					LastProfilerRefresh = 0.0;
				}
			}

			// The panel has one thing on it that can be clicked, and a click
			// that lands on it is swallowed rather than passed on: a button the
			// camera also turns for is not a button
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
				HandleProfilerClick(event.button.x, event.button.y)) {
				continue;
			}

			UserInputService->ProcessEvent(event);
			Workspace->CurrentCamera->OnEvent(Window, event);
		}
		}

		// Ahead of everything the frame does, so the reading belongs to the
		// frame that has just been measured rather than to the one being set up
		UpdateStatistics(seconds, deltaTime);
		UpdateProfiler(seconds);
		RenderProvider->SetWindowOverlay(
			1,
			ShowProfiler ? ProfilerPanel : nullptr,
			glm::vec2(STATISTICS_MARGIN, PROFILER_TOP)
		);

		{
			G_PROFILE("Simulation");
			RunService->FireSimulation(deltaTime);
			Workspace->CurrentCamera->Step(deltaTime);
			Workspace->DistributedGameTime += deltaTime;
			RunService->FirePostSimulation(deltaTime);
		}

		{
			G_PROFILE("PreRender");
			RunService->FireRender(deltaTime);
		}
		{
			// Tweens settle right before the frame is drawn, so the values
			// written this step are the ones rendered
			G_PROFILE("Tweens");
			TweenService->Step(deltaTime);
		}
		{
			G_PROFILE("Mesh Upload");
			MeshProvider::UploadToGpu(Gpu);
		}

		// Paces this frame against the GPU before any of it is submitted.
		// Nothing else in a frame waits, so without this the backlog of
		// submitted-but-unfinished work grows without bound.
		{
			// This is the only place a frame ever waits, so it is the whole of
			// what "GPU" can honestly mean here: SDL exposes no timestamp
			// queries, and how long the CPU sat blocked on last frame's fence
			// is the measurement that is actually available. A frame that is
			// GPU bound spends its time in here and nowhere else.
			G_PROFILE("GPU Wait");
			RenderProvider->BeginFrame(RenderSettings->GetFramesInFlight());
		}
		// Read every frame, so swapping the pass takes effect on the next one
		// rather than needing the renderer restarted
		RenderProvider->SetAntialiasOverride(RenderSettings->GetAntialiasShader());

		auto worldRoot = std::static_pointer_cast<WorldRoot>(Workspace);
		auto lightDirection = Lighting->GetSunDirection();

		// Published so an ad-hoc Camera:Render() draws this same world
		RenderProvider->Scene.WorldRoot = worldRoot;
		RenderProvider->Scene.LightDirection = lightDirection;
		RenderProvider->Scene.Time = Workspace->DistributedGameTime;
		// Cameras compare this against the one they last drew at, so a scene
		// that has not moved is not redrawn
		{
			G_PROFILE("Scene Signature");
			RenderProvider->SceneSignature = RenderProvider->ComputeSceneSignature(worldRoot, lightDirection);
		}

		auto currentCamera = Workspace->CurrentCamera;

		// A camera drawn to the window renders into an offscreen target first,
		// and that target is sized from ViewportSize. Nothing filled it in:
		// Camera::OnEvent only sets it for a freecam, and only when a resize
		// event arrives, so a window that was never resized left it at zero,
		// AcquireCameraTarget refused a zero-sized target, and the whole draw
		// bailed out having rendered nothing at all.
		int windowWidth = 0, windowHeight = 0;
		SDL_GetWindowSizeInPixels(Window, &windowWidth, &windowHeight);
		if (windowWidth > 0 && windowHeight > 0) {
			auto fitToWindow = [&](const std::shared_ptr<Camera> &camera) {
				if (!camera) {
					return;
				}

				// Its own share of the window, so a split-screen pane renders at
				// the size it is about to occupy rather than the whole width
				auto region = RenderProvider::ComputeWindowRegion(*camera, windowWidth, windowHeight);
				if (region.Width > 0 && region.Height > 0) {
					camera->ViewportSize = Vector2((float)region.Width, (float)region.Height);
				}
			};

			fitToWindow(currentCamera);
			for (auto *camera : Camera::GetAllCameras()) {
				if (!camera->DrawToWindow) {
					continue;
				}
				if (auto owned = camera->weak_from_this().lock()) {
					fitToWindow(std::static_pointer_cast<Camera>(owned));
				}
			}
		}

		// Anything that draws into the window, CurrentCamera first so it takes
		// the whole thing when nothing else asks for a share
		std::vector<DrawContext> windowCameras;
		if (currentCamera && currentCamera->Enabled) {
			windowCameras.push_back({
				.WorldRoot = worldRoot,
				.Camera = currentCamera,
				.LightDirection = lightDirection,
			});
		}

		// Everything that renders on its own this frame
		std::vector<Camera *> offscreenRoots;
		for (auto *camera : Camera::GetAllCameras()) {
			if (!camera->Enabled || camera == currentCamera.get()) {
				continue;
			}

			if (camera->DrawToWindow) {
				// A Camera that is not owned by a shared_ptr cannot be handed
				// to the renderer, so skip it rather than throwing bad_weak_ptr
				if (auto owned = camera->weak_from_this().lock()) {
					windowCameras.push_back({
						.WorldRoot = worldRoot,
						.Camera = std::static_pointer_cast<Camera>(owned),
						.LightDirection = lightDirection,
					});
				}
			} else {
				offscreenRoots.push_back(camera);
			}
		}

		// Anything a window camera samples must also be up to date before the
		// window is drawn
		for (const auto &context : windowCameras) {
			for (Camera *dependency : RenderProvider->GetSampledCameras(context.Camera.get())) {
				offscreenRoots.push_back(dependency);
			}
		}

		// So must any camera being shown on a part's surface
		for (const auto &part : worldRoot->Parts) {
			if (part && part->SurfaceCamera) {
				offscreenRoots.push_back(part->SurfaceCamera.get());
			}
		}

		// Sorted so a camera reading another's target sees this frame's picture.
		// Enabled decides whether a camera renders on its own; one that another
		// camera samples is drawn regardless, or that camera would be wrong.
		// A camera feeding a texture rarely needs to be as current as the view
		// the player is looking at, so it redraws at its own rate. Skipping
		// leaves the last picture in its target, which is what a reader samples
		// and what a slow security feed should look like anyway.
		double now = Workspace->DistributedGameTime;
		float maximumCameraFps = RenderSettings->GetMaxCameraFPS();

		std::vector<DrawContext> offscreenCameras;
		for (Camera *camera : RenderProvider->GetRenderOrder(offscreenRoots)) {
			auto owned = camera->weak_from_this().lock();
			if (!owned) {
				continue;
			}

			double interval = camera->GetRenderInterval(maximumCameraFps);

			// On demand: nothing here draws it. Camera:Render() still draws it
			// and everything it samples, which is the point of the mode.
			if (interval < 0.0) {
				continue;
			}

			// A camera that has never drawn always draws, or its target would
			// be blank until the first interval elapsed
			if (interval > 0.0 && camera->LastOffscreenDraw >= 0.0 &&
				now - camera->LastOffscreenDraw < interval) {
				continue;
			}
			camera->LastOffscreenDraw = now;

			offscreenCameras.push_back({
				.WorldRoot = worldRoot,
				.Camera = std::static_pointer_cast<Camera>(owned),
				.LightDirection = lightDirection,
			});
		}
		{
			// One command buffer for the lot, still in dependency order
			G_PROFILE("Offscreen Cameras");
			RenderProvider->DrawOffscreen(offscreenCameras);
		}

		{
			G_PROFILE("Window");
			if (windowCameras.size() == 1) {
				// One camera filling the window is the common case and can draw
				// straight into the swapchain
				RenderProvider->Draw(windowCameras.front());
			} else if (!windowCameras.empty()) {
				RenderProvider->DrawComposite(windowCameras);
			}
		}

		RenderProvider->EndFrame();

		{
			// Resume any script waiting on a Camera:Render() readback
			G_PROFILE("Scripts");
			RenderProvider->PollRenders(&ScriptEngine->ThreadEngine);
			ScriptEngine->Step();
		}

		LastTick = CurrentTick;
	}
} // namespace gargantuan
