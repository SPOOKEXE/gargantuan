#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceRegistry.hpp"
#include "gargantuan/physics/PhysicsWorld.hpp"
#include "gargantuan/render/PartRow.hpp"

#include <glm/glm.hpp>
#include <span>

namespace gargantuan {
	class WorldRoot : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		WorldRoot();

		ecs::InstanceRegistry<BasePart> Parts;
		PhysicsWorld Physics;

		// One column per thing a pass reads, so a pass only walks the arrays it
		// actually needs. All three are indexed by the same row index.
		std::span<const glm::mat4> GetModelMatrices() const {
			return ModelMatrices.Values();
		}
		std::span<const glm::vec4> GetPartColors() const {
			return Colors.Values();
		}
		std::span<const PartMeshRow> GetPartMeshes() const {
			return Meshes.Values();
		}

		void SyncPartRows();
		void StepPhysics(float deltaTime);

	  private:
		ecs::Column<glm::mat4> ModelMatrices;
		ecs::Column<glm::vec4> Colors;
		ecs::Column<PartMeshRow> Meshes;

		// Two channels, not one. A Color write rebuilds a render row but must
		// not wake the broadphase, and a part that only moved must not have its
		// appearance recomputed by the physics side.
		ecs::ChangeChannel *RenderChannel = nullptr;
		ecs::ChangeChannel *PhysicsChannel = nullptr;
		// Carries script-authored transforms into the solver before it steps.
		ecs::ChangeChannel *SolverChannel = nullptr;
	};
} // namespace gargantuan
