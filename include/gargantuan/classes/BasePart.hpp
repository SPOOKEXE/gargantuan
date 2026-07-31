#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/render/GpuMesh.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string_view>

namespace gargantuan {
	class Camera;
	class EditableImage;
	class PhysicsWorld;
	class WorldRoot;

	G_ENUM(NormalId, Right, Top, Back, Left, Bottom, Front, Slope, Sphere, Circumference);

	namespace components {
		struct Transform {
			gargantuan::CFrame CFrame;
			glm::vec3 Size = glm::vec3(2, 1, 4);
		};

		struct Visual {
			gargantuan::Color3 Color;
			float Transparency = 0.0f;
			float Reflectance = 0.0f;
			Enums::Material Material = Enums::Material::Plastic;
			bool CastShadow = true;
			uint8_t MeshId = 0;
		};

		struct Collider {
			bool CanCollide = true;
			bool CanQuery = true;
			bool CanTouch = true;
		};

		struct Surface {
			std::shared_ptr<gargantuan::Camera> Camera;
			std::shared_ptr<gargantuan::EditableImage> Image;
			Enums::NormalId Face = Enums::NormalId::Front;
			gargantuan::Vector2 Tiling = gargantuan::Vector2(1.0f, 1.0f);
			gargantuan::Vector2 Offset = gargantuan::Vector2(0.0f, 0.0f);
			uint8_t TextureSlot = 0;
		};
	}

	namespace EditorFlags {
		inline constexpr uint8_t Locked = 1 << 0;
	}

	class BasePart : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		components::Transform Transform;
		components::Visual Visual;
		components::Collider Collider;

		WorldRoot *World = nullptr;
		PhysicsWorld *Physics = nullptr;

		bool IsAnchored() const;
		void SetAnchored(bool anchored);
		bool IsMassless() const;
		void SetMassless(bool massless);
		bool IsLocked() const;
		void SetLocked(bool locked);

		std::string_view GetCollisionGroup() const;
		void SetCollisionGroup(std::string_view name);

		const components::Surface *FindSurface() const;
		components::Surface &EnsureSurface();

		// Moves pre-parent surface state into the world-indexed sparse set.
		void FlushPendingSurface();
		~BasePart() override;
		bool HasSurface() const {
			return FindSurface() != nullptr;
		}
		const components::Surface &GetSurfaceOrDefault() const;

		// Surface match mode is encoded in w; xyz is a part-space axis.
		static constexpr float SURFACE_MATCH_NORMAL = 0.0f;
		static constexpr float SURFACE_MATCH_ANY = 1.0f;
		static constexpr float SURFACE_MATCH_AROUND = 2.0f;
		glm::vec4 GetSurfaceMatch() const;
		// For callers that already hold the surface. Finding it is a sparse-set
		// lookup, and the per-part loops want one of those, not five.
		static glm::vec4 SurfaceMatchOf(Enums::NormalId face);

		glm::vec3 GetOrientation() const;
		void SetOrientation(glm::vec3 orientation);
		float GetMass() const;
		PhysicalProperties GetPhysicalProperties() const;
		void SetCustomPhysicalProperties(PhysicalProperties properties);
		void ClearCustomPhysicalProperties();

		glm::mat4 GetModelMatrix() const;
		virtual std::unique_ptr<GpuMesh> &GetMesh() const = 0;

	  private:
		friend class PhysicsWorld;
		friend class WorldRoot;
		// Detached state round-trips across removal and reparenting.
		bool DetachedAnchored = false;
		uint16_t DetachedCollisionGroup = 0;
	};
}
