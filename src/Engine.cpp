#include "gargantuan/Engine.hpp"
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

	void Engine::Step() {
		if (!IsRunning) {
			return;
		}

		CurrentTick = SDL_GetTicksNS();
		if (LastTick == 0) {
			LastTick = CurrentTick;
		}
		float deltaTime = GetDeltaTime();

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				IsRunning = false;
				return;
			}
			UserInputService->ProcessEvent(event);
			Workspace->CurrentCamera->OnEvent(Window, event);
		}

		RunService->FireSimulation(deltaTime);
		Workspace->CurrentCamera->Step(deltaTime);
		Workspace->DistributedGameTime += deltaTime;
		RunService->FirePostSimulation(deltaTime);

		RunService->FireRender(deltaTime);
		// Tweens settle right before the frame is drawn, so the values written
		// this step are the ones rendered
		TweenService->Step(deltaTime);
		MeshProvider::UploadToGpu(Gpu);

		// Paces this frame against the GPU before any of it is submitted.
		// Nothing else in a frame waits, so without this the backlog of
		// submitted-but-unfinished work grows without bound.
		RenderProvider->BeginFrame(RenderSettings->GetFramesInFlight());
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
		RenderProvider->SceneSignature = RenderProvider->ComputeSceneSignature(worldRoot, lightDirection);

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
		// One command buffer for the lot, still in dependency order
		RenderProvider->DrawOffscreen(offscreenCameras);

		if (windowCameras.size() == 1) {
			// One camera filling the window is the common case and can draw
			// straight into the swapchain
			RenderProvider->Draw(windowCameras.front());
		} else if (!windowCameras.empty()) {
			RenderProvider->DrawComposite(windowCameras);
		}

		RenderProvider->EndFrame();

		// Resume any script waiting on a Camera:Render() readback
		RenderProvider->PollRenders(&ScriptEngine->ThreadEngine);

		ScriptEngine->Step();

		LastTick = CurrentTick;
	}
} // namespace gargantuan
