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

		CurrentTick = SDL_GetTicks();
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

		// Paces this frame against the GPU before any of it is submitted. A
		// scene with offscreen cameras submits several command buffers a frame
		// and none of them is waited on anywhere else.
		RenderProvider->BeginFrame();

		auto worldRoot = std::static_pointer_cast<WorldRoot>(Workspace);
		auto lightDirection = Lighting->GetSunDirection();

		// Published so an ad-hoc Camera:Render() draws this same world
		RenderProvider->Scene.WorldRoot = worldRoot;
		RenderProvider->Scene.LightDirection = lightDirection;
		RenderProvider->Scene.Time = Workspace->DistributedGameTime;

		auto currentCamera = Workspace->CurrentCamera;

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
		for (Camera *camera : RenderProvider->GetRenderOrder(offscreenRoots)) {
			auto owned = camera->weak_from_this().lock();
			if (!owned) {
				continue;
			}

			RenderProvider->DrawOffscreen({
				.WorldRoot = worldRoot,
				.Camera = std::static_pointer_cast<Camera>(owned),
				.LightDirection = lightDirection,
			});
		}

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
