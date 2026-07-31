#include "gargantuan/services/RunService.hpp"
#include "gargantuan/Profiler.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const RunService::ClassDefinition RunService::DEFINITION = {
		.Name = "RunService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<RunService>(),
		.Properties =
			{
				G_UD_READONLY_PROP(RunService, PreSimulation, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, PostSimulation, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, PreAnimation, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, PreRender, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, Heartbeat, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, RenderStepped, Signal<double>::Pointer),
				G_UD_READONLY_PROP(RunService, Stepped, Signal<SteppedArguments>::Pointer),
			},
		.Methods = {
			{"IsClient", Method::Wrap<&RunService::IsClient>()},
			{"IsServer", Method::Wrap<&RunService::IsServer>()},
			{"IsStudio", Method::Wrap<&RunService::IsStudio>()},
			{"IsEdit", Method::Wrap<&RunService::IsEdit>()},
			{"IsRunning", Method::Wrap<&RunService::IsRunning>()},
			{"IsRunMode", Method::Wrap<&RunService::IsRunMode>()},
		}
	};

	// A zone per signal. Six of these fire every frame and a script may be
	// connected to any of them, so "the frame spent 4 ms in Luau" is only half
	// an answer: the other half is which point in the frame asked for it.
	void RunService::FireSimulation(double deltaTime) {
		Elapsed += deltaTime;
		{
			G_PROFILE("signal.Stepped");
			Stepped->Fire({Elapsed, deltaTime});
		}
		G_PROFILE("signal.PreSimulation");
		PreSimulation->Fire(deltaTime);
	}

	void RunService::FirePostSimulation(double deltaTime) {
		{
			G_PROFILE("signal.PostSimulation");
			PostSimulation->Fire(deltaTime);
		}
		G_PROFILE("signal.Heartbeat");
		Heartbeat->Fire(deltaTime);
	}

	void RunService::FireRender(double deltaTime) {
		{
			G_PROFILE("signal.PreAnimation");
			PreAnimation->Fire(deltaTime);
		}
		{
			G_PROFILE("signal.PreRender");
			PreRender->Fire(deltaTime);
		}
		G_PROFILE("signal.RenderStepped");
		RenderStepped->Fire(deltaTime);
	}

	double RunService::GetElapsedTime() const {
		return Elapsed;
	}

	bool RunService::IsClient() const {
		return true;
	}

	bool RunService::IsServer() const {
		return false;
	}

	bool RunService::IsStudio() const {
		return false;
	}

	bool RunService::IsEdit() const {
		return false;
	}

	bool RunService::IsRunning() const {
		return true;
	}

	bool RunService::IsRunMode() const {
		return false;
	}
}
