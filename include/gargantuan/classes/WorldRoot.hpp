#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceRegistry.hpp"
#include "gargantuan/physics/PhysicsWorld.hpp"

#include <glm/glm.hpp>

namespace gargantuan {
	class WorldRoot : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		WorldRoot();

		ecs::InstanceRegistry<BasePart> Parts;
		PhysicsWorld Physics;

		ecs::SparseSet<components::Surface> Surfaces;
		ecs::SparseSet<PhysicalProperties> MassOverrides;
		ecs::SparseSet<uint8_t> EditorFlagBits;

		glm::mat4 GetPreviousModelMatrix(uint32_t index, const glm::mat4 &fallback) const;
		void StampPreviousModelMatrix(uint32_t index, const glm::mat4 &model);
		void ClearPreviousModelMatrices();

		ecs::ChangeChannel &GetRenderChannel() {
			return *RenderChannel;
		}

		void StepPhysics(float deltaTime);
		void EnsureBroadphase();

	  private:
		ecs::Column<glm::mat4> PreviousModelMatrices;
		ecs::Column<uint8_t> HasPreviousModelMatrix;

		ecs::ChangeChannel *RenderChannel = nullptr;
		ecs::ChangeChannel *PhysicsChannel = nullptr;
		ecs::ChangeChannel *SolverChannel = nullptr;
	};
}
