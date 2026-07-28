#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/classes/BasePart.hpp"

#include <SDL3/SDL_log.h>
#include <memory>

namespace gargantuan {
	const WorldRoot::ClassDefinition WorldRoot::DEFINITION = {
		.Name = "WorldRoot",
		.Superclass = "Instance",
	};

	WorldRoot::WorldRoot() {
		// Parts is the list the renderer walks, so it has to track both
		// directions -- otherwise a removed part keeps being drawn
		ChildAdded->Connect([this](Instance::Pointer instance) {
			if (instance->IsA("BasePart")) {
				std::shared_ptr<BasePart> part = std::static_pointer_cast<BasePart>(instance);
				this->Parts.push_back(part);
			}
		});

		ChildRemoved->Connect([this](Instance::Pointer instance) {
			std::erase_if(this->Parts, [&instance](const std::shared_ptr<BasePart> &part) {
				return part.get() == instance.get();
			});
		});
	}
} // namespace gargantuan
