#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceRegistry.hpp"

#include <box3d/id.h>
#include <glm/glm.hpp>
#include <string_view>

namespace gargantuan {
	// Collision group names are interned once, process-wide, and parts carry
	// the small integer. The string only exists where a script wrote it.
	//
	// "Default" is id 0 and is the absent case: a part in the default group has
	// no entry in the world's set at all, which is the overwhelming majority.
	namespace CollisionGroupTable {
		inline constexpr uint16_t DefaultId = 0;

		uint16_t GetId(std::string_view name);
		std::string_view GetName(uint16_t id);
	} // namespace CollisionGroupTable

	// Sparse: only the parts that actually move. An anchored part has no
	// RigidBody at all, so the integrator never sees it and never pays a branch
	// or a cache miss to find out it should skip it.
	struct RigidBody {
		glm::vec3 Velocity{0.0f};
		glm::vec3 AngularVelocity{0.0f};
		glm::vec3 InvInertia{1.0f};
		float InvMass = 1.0f;
		// The solver's handle for this body. box3d is vendored and linked but
		// no solver is driven yet; the handle lives here so that when one is,
		// it is a field on the mover rather than something bolted onto every
		// part in the world.
		b3BodyId Body = b3_nullBodyId;
	};

	// Dense: every part, because the broadphase sweeps all of them. Small
	// enough that two rows share a cache line, which is the whole point of not
	// reading this off the part itself.
	struct BroadphaseRow {
		glm::vec3 Min{0.0f};
		glm::vec3 Max{0.0f};
		uint16_t CollisionGroupId = 0;
		uint8_t Flags = 0;
	};

	namespace ColliderFlags {
		inline constexpr uint8_t CanCollide = 1 << 0;
		inline constexpr uint8_t CanQuery = 1 << 1;
		inline constexpr uint8_t CanTouch = 1 << 2;
	} // namespace ColliderFlags

	class PhysicsWorld {
	  public:
		~PhysicsWorld();

		ecs::SparseSet<RigidBody> Bodies;
		ecs::Column<BroadphaseRow> Broadphase;
		// Sparse, because almost every part is in the default group.
		ecs::SparseSet<uint16_t> CollisionGroups;

		// Zero by default: the layout work is the point here, and switching
		// gravity on would drop every unanchored part in existing scenes.
		glm::vec3 Gravity{0.0f, 0.0f, 0.0f};
		void SetGravity(glm::vec3 gravity);

		// Sub-steps per frame, box3d's solver iteration count.
		int SubStepCount = 4;

		bool IsAnchored(const BasePart &part) const;
		void SetAnchored(BasePart &part, bool anchored);

		// Massless folds into the body as InvMass == 0 rather than being a
		// separate bool carried by every part in the world.
		bool IsMassless(const BasePart &part) const;
		void SetMassless(BasePart &part, bool massless);

		uint16_t GetCollisionGroupId(const BasePart &part) const;
		void SetCollisionGroupId(BasePart &part, uint16_t id);

		// Called when a part joins or leaves the world, to create or retire its
		// body and group entry from what the part was carrying detached.
		void OnPartAdded(BasePart &part);
		void OnPartRemoved(BasePart &part);

		// solverChannel carries transforms authored from outside the solver
		// (scripts, tweens) into box3d before the step, so a written CFrame
		// wins over whatever the solver had.
		void Step(ecs::InstanceRegistry<BasePart> &parts, float deltaTime, ecs::ChangeChannel &solverChannel);
		void SyncBroadphase(ecs::InstanceRegistry<BasePart> &parts, ecs::ChangeChannel &channel);

	  private:
		// Created on the first mover, so a world with nothing dynamic in it
		// never builds a solver at all.
		void EnsureSolver();
		void CreateBody(const BasePart &part, RigidBody &body);
		void ApplyMassData(RigidBody &body);
		void DestroyBody(RigidBody &body);

		b3WorldId Solver = b3_nullWorldId;
	};
} // namespace gargantuan
