#include "gargantuan/services/RunService.hpp"
#include "gargantuan/reflection/InstanceClassRegistry.hpp"

namespace gargantuan {
	G_INSTANCE_IMPL(
		RunService,
		.Properties = {
			{"PreSimulation", Property::fromReadonlyMember<&RunService::PreSimulation>()},
			{"PostSimulation", Property::fromReadonlyMember<&RunService::PostSimulation>()},
			{"PreRender", Property::fromReadonlyMember<&RunService::PreRender>()},
		}
	);
}
