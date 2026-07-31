#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"

#include <SDL3/SDL_log.h>
#include <memory>

namespace gargantuan {
	const WorldRoot::ClassDefinition WorldRoot::DEFINITION = {
		.Name = "WorldRoot",
		.Superclass = "Instance",
	};

	WorldRoot::WorldRoot() {
		// Component storage first, so every row that joins is sized for.
		Parts.Register(&PreviousModelMatrices);
		Parts.Register(&HasPreviousModelMatrix);
		Parts.Register(&Physics.Broadphase);
		Parts.Register(&Physics.Bodies);
		Parts.Register(&Physics.CollisionGroups);
		Parts.Register(&Surfaces);
		Parts.Register(&MassOverrides);
		Parts.Register(&EditorFlagBits);

		RenderChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform | ecs::ChangeFlags::Visual);
		PhysicsChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform | ecs::ChangeFlags::Collision);
		SolverChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform);

		Parts.OnAdded = [this](BasePart *part, uint32_t) {
			part->World = this;
			// After World is set, because that is what gives the surface somewhere
			// to go.
			part->FlushPendingSurface();
			Physics.OnPartAdded(*part);
		};
		Parts.OnRemoved = [this](BasePart *part, uint32_t) {
			Physics.OnPartRemoved(*part);
			part->World = nullptr;
		};

		Parts.Attach(this);
	}

	void WorldRoot::StepPhysics(float deltaTime) {
		Physics.Step(Parts, deltaTime, *SolverChannel);
	}

	// Callers that actually read the broadphase bring it up to date first. The
	// change list keeps whatever moved in the meantime, so this costs the same
	// whether it is asked for every frame or once a second.
	void WorldRoot::EnsureBroadphase() {
		Physics.EnsureBroadphase(Parts, *PhysicsChannel);
	}
} // namespace gargantuan

namespace gargantuan {
	glm::mat4 WorldRoot::GetPreviousModelMatrix(uint32_t index, const glm::mat4 &fallback) const {
		if (index >= HasPreviousModelMatrix.Size() || !HasPreviousModelMatrix[index]) return fallback;
		return PreviousModelMatrices[index];
	}

	void WorldRoot::StampPreviousModelMatrix(uint32_t index, const glm::mat4 &model) {
		if (index >= PreviousModelMatrices.Size()) return;
		PreviousModelMatrices[index] = model;
		HasPreviousModelMatrix[index] = 1;
	}

	void WorldRoot::ClearPreviousModelMatrices() {
		for (uint32_t index = 0; index < HasPreviousModelMatrix.Size(); index++) {
			HasPreviousModelMatrix[index] = 0;
		}
	}
} // namespace gargantuan
