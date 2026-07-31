#include "gargantuan/services/RunService.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const RunService::ClassDefinition RunService::DEFINITION = {
		.Name = "RunService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<RunService>(),
		.Properties = {
			{"PreSimulation", Property::fromSimple<&RunService::PreSimulation>(true, false)},
			{"PostSimulation", Property::fromSimple<&RunService::PostSimulation>(true, false)},
			{"PreRender", Property::fromSimple<&RunService::PreRender>(true, false)},
		}
	};
}
