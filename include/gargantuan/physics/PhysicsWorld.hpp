#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceRegistry.hpp"

#include <box3d/id.h>
#include <glm/glm.hpp>
#include <string_view>

namespace gargantuan {
	namespace CollisionGroupTable {
		inline constexpr uint16_t DefaultId = 0;

		uint16_t GetId(std::string_view name);
		std::string_view GetName(uint16_t id);
	}

	struct RigidBody {
		glm::vec3 Velocity{0.0f};
		glm::vec3 AngularVelocity{0.0f};
		glm::vec3 InvInertia{1.0f};
		float InvMass = 1.0f;
		b3BodyId Body = b3_nullBodyId;
	};

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
	}

	class PhysicsWorld {
	  public:
		~PhysicsWorld();

		ecs::SparseSet<RigidBody> Bodies;
		ecs::Column<BroadphaseRow> Broadphase;
		ecs::SparseSet<uint16_t> CollisionGroups;

		glm::vec3 Gravity{0.0f, 0.0f, 0.0f};
		void SetGravity(glm::vec3 gravity);

		int SubStepCount = 4;

		bool IsAnchored(const BasePart &part) const;
		void SetAnchored(BasePart &part, bool anchored);

		bool IsMassless(const BasePart &part) const;
		void SetMassless(BasePart &part, bool massless);

		uint16_t GetCollisionGroupId(const BasePart &part) const;
		void SetCollisionGroupId(BasePart &part, uint16_t id);

		void OnPartAdded(BasePart &part);
		void OnPartRemoved(BasePart &part);

		void Step(ecs::InstanceRegistry<BasePart> &parts, float deltaTime, ecs::ChangeChannel &solverChannel);

		void EnsureBroadphase(ecs::InstanceRegistry<BasePart> &parts, ecs::ChangeChannel &channel);

	  private:
		void EnsureSolver();
		void CreateBody(const BasePart &part, RigidBody &body);
		void ApplyMassData(RigidBody &body);
		void DestroyBody(RigidBody &body);

		b3WorldId Solver = b3_nullWorldId;
	};
}
