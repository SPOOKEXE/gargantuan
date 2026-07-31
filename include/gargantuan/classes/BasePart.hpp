#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/render/GpuMesh.hpp"

#include <glm/glm.hpp>
#include <string_view>

namespace gargantuan {
	class PhysicsWorld;

	// Components are cut by which loop reads them together on the same pass,
	// not by what conceptually belongs to a part. They live in their own
	// namespace so a member can share a component's name without shadowing it.
	namespace components {
		// Read by: render fill, shadow pass, physics integration, bounds.
		struct Transform {
			gargantuan::CFrame CFrame;
			glm::vec3 Size = glm::vec3(2, 1, 4);
		};

		// Read by: render fill, shadow pass.
		struct Visual {
			gargantuan::Color3 Color;
			float Transparency = 0.0f;
			bool CastShadow = true;
		};

		// Read by: broadphase, raycast, touch events. Three bits, packed into
		// the broadphase row's flags byte rather than read back off the part.
		struct Collider {
			bool CanCollide = true;
			bool CanQuery = true;
			bool CanTouch = true;
		};
	} // namespace components

	class BasePart : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		components::Transform Transform;
		components::Visual Visual;
		components::Collider Collider;

		// Set when the part joins a world. Anchored is the absence of a
		// RigidBody in that world rather than a bool carried on every part, so
		// the physics step iterates exactly the movers.
		PhysicsWorld *Physics = nullptr;

		bool IsAnchored() const;
		void SetAnchored(bool anchored);

		// A small integer in a sparse set, not a std::string on every part.
		// The name is resolved through a process-wide table and only ever
		// materialised for the script that asked for it.
		std::string_view GetCollisionGroup() const;
		void SetCollisionGroup(std::string_view name);

		glm::mat4 GetModelMatrix() const;
		virtual std::unique_ptr<GpuMesh> &GetMesh() const = 0;

	  private:
		friend class PhysicsWorld;
		// Where these live while the part has no world to hold a body or a
		// group entry for it. Written back on removal, so detaching and
		// reparenting round-trips.
		bool DetachedAnchored = false;
		uint16_t DetachedCollisionGroup = 0;
	};
} // namespace gargantuan
