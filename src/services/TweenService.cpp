#include "gargantuan/services/TweenService.hpp"

namespace gargantuan {
	const TweenService::ClassDefinition TweenService::DEFINITION = {
		.Name = "TweenService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<TweenService>(),
		.Properties = {

		}
	};
}
