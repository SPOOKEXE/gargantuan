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

	// Which face of a part its surface picture lands on. The first six are
	// ordered the way Roblox orders NormalId, so a value copied from there
	// means the same here; the three after them name the faces a box does not
	// have -- a wedge's slope, a ball's whole surface, a cylinder's curved
	// side.
	G_ENUM(NormalId, Right, Top, Back, Left, Bottom, Front, Slope, Sphere, Circumference);

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

		// How the fragment stage recognises SurfaceFace. A flat face is a
		// direction to match, but a ball has no one direction and a cylinder's
		// side has a whole ring of them, so the rule travels with it.
		//
		// xyz is in the part's own space; w says what to do with it:
		//   0  the face pointing that way
		//   1  every direction, for a surface that curves through all of them
		//   2  every direction square to xyz, for a side that wraps that axis
		static constexpr float SURFACE_MATCH_NORMAL = 0.0f;
		static constexpr float SURFACE_MATCH_ANY = 1.0f;
		static constexpr float SURFACE_MATCH_AROUND = 2.0f;
		glm::vec4 GetSurfaceMatch() const;

		// Euler angles in degrees, the way Roblox reports part rotation
		glm::vec3 GetOrientation() const;
		void SetOrientation(glm::vec3 orientation);
		// Volume times density; a massless or anchored part still reports its
		// would-be mass, matching Roblox
		float GetMass() const;
		PhysicalProperties GetPhysicalProperties() const;

		// Which primitive mesh this part draws.
		uint8_t MeshId = 0;

		glm::mat4 GetModelMatrix();

		// Where this part stood last frame. Bookkeeping, not a property: the
		// renderer stamps it once a frame, after every camera has drawn, so
		// each of them measures motion against the same "last frame" rather
		// than against whichever camera happened to go first.
		//
		// Only kept while something asks for motion vectors, so a part is not
		// paying for a matrix nothing reads. A part that has never been stamped
		// -- one built this frame, or the frame a camera first asks -- reports
		// no motion at all, which is the right answer: there is no earlier
		// position for it to have moved from.
		glm::mat4 PreviousModelMatrix = glm::mat4(1.0f);
		bool HasPreviousModelMatrix = false;

		virtual std::unique_ptr<GpuMesh> &GetMesh() const = 0;
	};
} // namespace gargantuan
