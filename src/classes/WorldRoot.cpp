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
		Parts.Register(&ModelMatrices);
		Parts.Register(&Colors);
		Parts.Register(&Meshes);
		Parts.Register(&Physics.Broadphase);
		Parts.Register(&Physics.Bodies);
		Parts.Register(&Physics.CollisionGroups);

		RenderChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform | ecs::ChangeFlags::Visual);
		PhysicsChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform | ecs::ChangeFlags::Collision);
		SolverChannel = &Parts.CreateChannel(ecs::ChangeFlags::Transform);

		Parts.OnAdded = [this](BasePart *part, uint32_t) { Physics.OnPartAdded(*part); };
		Parts.OnRemoved = [this](BasePart *part, uint32_t) { Physics.OnPartRemoved(*part); };

		Parts.Attach(this);
	}

	void WorldRoot::SyncPartRows() {
		uint32_t count = Parts.Size();
		RenderChannel->Consume(count, [&](uint32_t index) {
			BasePart *part = Parts.At(index);

			ModelMatrices[index] = part->GetModelMatrix();
			Colors[index] = glm::vec4((glm::vec3)part->Visual.Color, 1.0f - part->Visual.Transparency);

			PartMeshRow &mesh = Meshes[index];
			// Resolved here rather than per pass per frame: GetMesh is a string
			// build plus a hash lookup, and it only changes when the part does.
			mesh.Slot = &part->GetMesh();
			mesh.CastShadow = part->Visual.CastShadow;
		});
	}

	void WorldRoot::StepPhysics(float deltaTime) {
		Physics.Step(Parts, deltaTime, *SolverChannel);
		Physics.SyncBroadphase(Parts, *PhysicsChannel);
	}
} // namespace gargantuan
