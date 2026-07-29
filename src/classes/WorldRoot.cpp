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
				part->WorldIndex = (uint32_t)this->Parts.size();
				this->Parts.push_back(part);
				this->RawParts.push_back(part.get());
				part->ChangeList = &this->DirtyParts;
				part->MarkChanged();
			}
		});

		ChildRemoved->Connect([this](Instance::Pointer instance) {
			if (!instance->IsA("BasePart")) {
				return;
			}

			BasePart *part = static_cast<BasePart *>(instance.get());
			uint32_t index = part->WorldIndex;
			if (index >= this->RawParts.size() || this->RawParts[index] != part) {
				return;
			}

			part->LeaveChangeList();
			part->ChangeList = nullptr;

			uint32_t last = (uint32_t)this->RawParts.size() - 1;
			if (index != last) {
				this->Parts[index] = std::move(this->Parts[last]);
				this->RawParts[index] = this->RawParts[last];
				this->RawParts[index]->WorldIndex = index;
				this->RawParts[index]->MarkChanged();
			}
			this->Parts.pop_back();
			this->RawParts.pop_back();
		});
	}

	WorldRoot::~WorldRoot() {
		for (BasePart *part : RawParts) {
			part->InChangeList = false;
			part->ChangeList = nullptr;
		}
	}
} // namespace gargantuan
