#pragma once

#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/render/GpuMesh.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>

namespace gargantuan {
	class Camera;

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
		// Shows another camera's picture on this part's faces, using the mesh's
		// own UVs. Null leaves the part its flat Color.
		std::shared_ptr<Camera> SurfaceCamera;

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
