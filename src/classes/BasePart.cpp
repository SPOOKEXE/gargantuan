#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lualib.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace gargantuan {
	const BasePart::ClassDefinition BasePart::DEFINITION = {
		.Name = "BasePart",
		.Superclass = "Instance",
		.Properties = {
			G_UD_READWRITE_PROP(BasePart, Anchored, bool),
			G_UD_READWRITE_PROP(BasePart, CanCollide, bool),
			G_UD_READWRITE_PROP(BasePart, CanQuery, bool),
			G_UD_READWRITE_PROP(BasePart, CanTouch, bool),
			G_UD_READWRITE_PROP(BasePart, CastShadow, bool),
			G_UD_READWRITE_PROP(BasePart, CFrame, gargantuan::CFrame),
			G_UD_READWRITE_PROP(BasePart, CollisionGroup, std::string),
			G_UD_READWRITE_PROP(BasePart, Color, gargantuan::Color3),
			G_UD_READWRITE_PROP(BasePart, Locked, bool),
			G_UD_READWRITE_PROP(BasePart, Massless, bool),
			G_UD_READWRITE_PROP(BasePart, Material, Enums::Material),
			G_UD_READWRITE_PROP(BasePart, Reflectance, float),
			G_UD_READWRITE_PROP(BasePart, Size, glm::vec3),
			G_UD_READWRITE_PROP(BasePart, Transparency, float),
			{
				"CustomPhysicalProperties",
				{
					[](lua_State *L, Instance *instance) -> int {
						auto part = instance->Cast<BasePart>();
						return StackValue<std::optional<PhysicalProperties>>::Push(
							L, part->CustomPhysicalProperties
						);
					},
					[](lua_State *L, Instance *instance) -> int {
						auto part = instance->Cast<BasePart>();
						part->CustomPhysicalProperties =
							CheckStackValue<std::optional<PhysicalProperties>>(L, -1);
						return 0;
					},
					G_UD_REFLECT_TYPE(std::optional<PhysicalProperties>),
				},
			},
			{
				"SurfaceCamera",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<Instance::Pointer>::Push(L, instance->Cast<BasePart>()->SurfaceCamera);
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						auto part = instance->Cast<BasePart>();
						if (lua_isnoneornil(L, -1)) {
							part->SurfaceCamera = nullptr;
							return 0;
						}

						auto camera = std::dynamic_pointer_cast<Camera>(
							StackValue<Instance::Pointer>::From(L, -1)
						);
						if (!camera) {
							luaL_error(L, "SurfaceCamera must be a Camera");
							return 0;
						}

						part->SurfaceCamera = camera;
						return 0;
					},
					G_UD_REFLECT_TYPE(Instance::Pointer),
				},
			},
			{
				"Mass",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<float>::Push(L, instance->Cast<BasePart>()->GetMass());
						return 1;
					},
					nullptr,
					G_UD_REFLECT_TYPE(float),
				},
			},
			{
				"Position",
				{
					[](lua_State *L, Instance *instance) -> int {
						auto part = instance->Cast<BasePart>();
						StackValue<glm::vec3>::Push(L, part->CFrame.Position);
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						auto part = instance->Cast<BasePart>();
						part->CFrame = gargantuan::CFrame(StackValue<glm::vec3>::From(L, -1), part->CFrame.Rotation);
						return 0;
					},
					G_UD_REFLECT_TYPE(glm::vec3),
				},
			},
			{
				"Orientation",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<glm::vec3>::Push(L, instance->Cast<BasePart>()->GetOrientation());
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						instance->Cast<BasePart>()->SetOrientation(StackValue<glm::vec3>::From(L, -1));
						return 0;
					},
					G_UD_REFLECT_TYPE(glm::vec3),
				},
			},
			// Roblox exposes the same Euler angles under both names
			{
				"Rotation",
				{
					[](lua_State *L, Instance *instance) -> int {
						StackValue<glm::vec3>::Push(L, instance->Cast<BasePart>()->GetOrientation());
						return 1;
					},
					[](lua_State *L, Instance *instance) -> int {
						instance->Cast<BasePart>()->SetOrientation(StackValue<glm::vec3>::From(L, -1));
						return 0;
					},
					G_UD_REFLECT_TYPE(glm::vec3),
				},
			},
		},
		.Methods = {
			{"GetMass", Method::Wrap<&BasePart::GetMass>()},
		}
	};

	glm::vec3 BasePart::GetOrientation() const {
		auto [x, y, z] = CFrame.ToOrientation();
		return glm::degrees(glm::vec3((float)x, (float)y, (float)z));
	}

	void BasePart::SetOrientation(glm::vec3 orientation) {
		glm::vec3 radians = glm::radians(orientation);
		CFrame = gargantuan::CFrame(CFrame.Position, gargantuan::CFrame::Angles(radians.x, radians.y, radians.z).Rotation);
	}

	PhysicalProperties BasePart::GetPhysicalProperties() const {
		if (CustomPhysicalProperties.has_value()) {
			return CustomPhysicalProperties.value();
		}
		return PhysicalProperties(Material);
	}

	float BasePart::GetMass() const {
		float volume = Size.x * Size.y * Size.z;
		return volume * GetPhysicalProperties().Density;
	}

	glm::mat4 BasePart::GetModelMatrix() {
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), CFrame.Position);
		glm::mat4 rotation = CFrame.Rotation;
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), Size);
		return translation * rotation * scale;
	}
} // namespace gargantuan
