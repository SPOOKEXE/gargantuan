#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/render/GpuMesh.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>

namespace gargantuan {
	class Camera;
	class EditableImage;

	// Which face of a part its surface picture lands on. Ordered the way
	// Roblox orders NormalId, so a value copied from there means the same here.
	G_ENUM(NormalId, Right, Top, Back, Left, Bottom, Front);

	class BasePart : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		bool Anchored = false;
		bool CanCollide = true;
		bool CanQuery = true;
		bool CanTouch = true;
		bool CastShadow = true;
		CFrame CFrame;
		Color3 Color;
		bool Locked = false;
		bool Massless = false;
		Enums::Material Material = Enums::Material::Plastic;
		float Reflectance = 0.0f;
		glm::vec3 Size = glm::vec3(2, 1, 4);
		float Transparency = 0.0f;
		std::string CollisionGroup = "Default";
		// Unset means the part inherits its Material's properties
		std::optional<PhysicalProperties> CustomPhysicalProperties;
		// Shows another camera's picture on this part, using the mesh's own UVs.
		// Null leaves the part its flat Color.
		std::shared_ptr<Camera> SurfaceCamera;
		// The same surface, from a drawn image instead. SurfaceCamera wins when
		// both are set, since a live feed is the more specific intent.
		std::shared_ptr<EditableImage> SurfaceImage;
		// Which face the picture lands on. Every other face keeps its Color.
		Enums::NormalId SurfaceFace = Enums::NormalId::Front;
		// How many times the picture repeats across that face, and where it
		// starts. Tiling of one and offset of zero is the whole face once.
		Vector2 SurfaceTiling = Vector2(1.0f, 1.0f);
		Vector2 SurfaceOffset = Vector2(0.0f, 0.0f);

		// The outward normal of SurfaceFace, in the part's own space
		glm::vec3 GetSurfaceNormal() const;

		// Euler angles in degrees, the way Roblox reports part rotation
		glm::vec3 GetOrientation() const;
		void SetOrientation(glm::vec3 orientation);
		// Volume times density; a massless or anchored part still reports its
		// would-be mass, matching Roblox
		float GetMass() const;
		PhysicalProperties GetPhysicalProperties() const;

		glm::mat4 GetModelMatrix();
		virtual std::unique_ptr<GpuMesh> &GetMesh() const = 0;
	};
} // namespace gargantuan
