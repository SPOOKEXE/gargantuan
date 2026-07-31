#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/reflection/InstanceClassRegistry.hpp"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <memory>

namespace gargantuan {
	G_INSTANCE_ABSTRACT_IMPL(WorldRoot);

	WorldRoot::WorldRoot() {
		DescendantAdded->Connect([this](Instance::Pointer instance) {
			if (instance->IsA("BasePart")) {
				std::shared_ptr<BasePart> part = std::static_pointer_cast<BasePart>(instance);
				this->Parts.push_back(part);
			}
		});

		DescendantRemoved->Connect([this](Instance::Pointer instance) {
			if (!instance->IsA("BasePart")) {
				return;
			}

			auto part = std::static_pointer_cast<BasePart>(instance);
			if (auto it = std::find(this->Parts.begin(), this->Parts.end(), part); it != this->Parts.end()) {
				this->Parts.erase(it);
			}
		});
	}
} // namespace gargantuan
