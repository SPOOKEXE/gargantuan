#include "gargantuan/services/RunService.hpp"
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

	void RunService::FireSimulation(double deltaTime) {
		Elapsed += deltaTime;
		Stepped->Fire({Elapsed, deltaTime});
		PreSimulation->Fire(deltaTime);
	}

	void RunService::FirePostSimulation(double deltaTime) {
		PostSimulation->Fire(deltaTime);
		Heartbeat->Fire(deltaTime);
	}

	void RunService::FireRender(double deltaTime) {
		PreAnimation->Fire(deltaTime);
		PreRender->Fire(deltaTime);
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
