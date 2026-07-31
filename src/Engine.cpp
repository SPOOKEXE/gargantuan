#include "gargantuan/Engine.hpp"
#include "gargantuan/Profiler.hpp"
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
		this->ScriptEngine = new class ScriptEngine();

		DataModel = std::make_shared<gargantuan::DataModel>();
		DataModel->Name = "Welcome To Hell";

		auto workspace = this->DataModel->GetService("Workspace");
		this->Workspace = std::dynamic_pointer_cast<gargantuan::Workspace>(workspace);

		auto lighting = this->DataModel->GetService("Lighting");
		this->Lighting = std::dynamic_pointer_cast<gargantuan::Lighting>(lighting);
		// The second user of InstanceRegistry: PointLights anywhere under the
		// world register themselves, with no storage code of their own.
		this->Lighting->Attach(this->Workspace.get());

		auto runService = this->DataModel->GetService("RunService");
		this->RunService = std::dynamic_pointer_cast<gargantuan::RunService>(runService);

		auto uis = this->DataModel->GetService("UserInputService");
		this->UserInputService = std::dynamic_pointer_cast<gargantuan::UserInputService>(uis);

		StackValue<Instance::Pointer>::Push(ScriptEngine->L, this->DataModel);
		lua_pushvalue(ScriptEngine->L, -1);
		lua_setglobal(ScriptEngine->L, "game");

		RegisterSystems();
	}

	// Systems run in phase order, and in declaration order within a phase.
	// Nothing here names another system as a dependency; if the ordering needs
	// to change, a system moves phase rather than growing a reference to a
	// neighbour.
	void Engine::RegisterSystems() {
		using ecs::Phase;

		Scheduler.Add(Phase::OnLoad, "input.poll", [this](float) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_EVENT_QUIT) {
					IsRunning = false;
					return;
				}

				// Held down, these would repeat and flicker the panel on and off
				if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
					if (event.key.key == SDLK_F3) {
						ShowStatistics = !ShowStatistics;
					} else if (event.key.key == SDLK_F5) {
						ShowSystems = !ShowSystems;
					}
				}

				UserInputService->ProcessEvent(event);
				Workspace->CurrentCamera->OnEvent(Window, event);
			}
		});

		Scheduler.Add(Phase::PreUpdate, "run.preSimulation", [this](float deltaTime) {
			RunService->PreSimulation->Fire(deltaTime);
		});

		Scheduler.Add(Phase::OnUpdate, "camera.step", [this](float deltaTime) {
			Workspace->CurrentCamera->Step(deltaTime);
		});

		Scheduler.Add(Phase::OnUpdate, "physics.step", [this](float deltaTime) {
			Workspace->StepPhysics(deltaTime);
		});

		Scheduler.Add(Phase::PostUpdate, "run.postSimulation", [this](float deltaTime) {
			RunService->PostSimulation->Fire(deltaTime);
		});

		Scheduler.Add(Phase::PreStore, "run.preRender", [this](float deltaTime) {
			RunService->PreRender->Fire(deltaTime);
		});

		Scheduler.Add(Phase::PreStore, "mesh.upload", [this](float) { MeshProvider::UploadToGpu(Gpu); });

		// Refreshes only the rows whose part changed since the last frame.
		Scheduler.Add(Phase::PreStore, "world.syncPartRows", [this](float) { Workspace->SyncPartRows(); });

		Scheduler.Add(Phase::PreStore, "lighting.syncRows", [this](float) { Lighting->SyncLightRows(); });

		Scheduler.Add(Phase::OnStore, "render.draw", [this](float) {
			if (!IsRunning) return;
			RenderProvider->Draw({
				.WorldRoot = std::static_pointer_cast<gargantuan::WorldRoot>(Workspace),
				.Camera = Workspace->CurrentCamera,
				.Overlay = &Overlay,
			});
		});

		Scheduler.Add(Phase::OnStore, "script.step", [this](float) { ScriptEngine->Step(); });
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

	// The panel reports the frame that just finished, so it is composed before
	// the schedule runs rather than after: the systems' timings are last
	// frame's either way, and building it here keeps it out of render.draw.
	void Engine::UpdateOverlay(double now, float deltaTime) {
		// Recorded even while hidden. The panel gets opened because something
		// went wrong a moment ago, and the history is the whole answer.
		Statistics.Record(now, deltaTime);

		if (!ShowStatistics && !ShowSystems) {
			Overlay.Resize(0, 0);
			return;
		}

		if (ShowSystems) {
			SystemTimings.clear();
			for (size_t index = 0; index < (size_t)ecs::Phase::Count; index++) {
				auto phase = (ecs::Phase)index;
				for (const auto &system : Scheduler.GetSystems(phase)) {
					SystemTimings.push_back({ecs::GetPhaseName(phase), system.Name, system.LastMilliseconds});
				}
			}
		}

		DrawDebugPanels(
			Overlay,
			ShowStatistics ? &Statistics : nullptr,
			ShowSystems ? &SystemTimings : nullptr,
			G_PROFILE_ACTIVE()
		);
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

		UpdateOverlay((double)CurrentTick / 1000.0, deltaTime);

		{
			G_PROFILE("Main Thread");
			Scheduler.Run(deltaTime);
		}

		// Outside the zone: it separates frames rather than belonging to one
		G_PROFILE_FRAME();

		LastTick = CurrentTick;
	}
} // namespace gargantuan
