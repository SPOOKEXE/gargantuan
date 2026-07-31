#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"
#include "gargantuan/physics/PhysicsWorld.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	// A scriptable component field: the same read/write pair every time, plus
	// the one thing that is easy to forget by hand -- telling the world which
	// cached rows this write just invalidated.
#define G_COMPONENT_PROPERTY(propertyName, valueType, member, changeFlags)                                              \
	{propertyName,                                                                                                     \
	 Property::fromReadWrite<valueType>(                                                                               \
		 [](Instance *self) -> valueType { return self->Cast<BasePart>()->member; },                                    \
		 [](Instance *self, valueType value) {                                                                         \
			 auto *part = self->Cast<BasePart>();                                                                       \
			 part->member = value;                                                                                     \
			 part->MarkChanged(changeFlags);                                                                            \
		 }                                                                                                             \
	 )}

	const BasePart::ClassDefinition BasePart::DEFINITION = {
		.Name = "BasePart",
		.Superclass = "Instance",
		.Properties = {
			// Not stored. Reads and writes membership of the world's RigidBody
			// set, which is where "does this part move" actually lives.
			{
				"Anchored",
				Property::fromReadWrite<bool>(
					[](Instance *self) { return self->Cast<BasePart>()->IsAnchored(); },
					[](Instance *self, bool value) { self->Cast<BasePart>()->SetAnchored(value); }
				),
			},
			{
				// Not a bool on every part: it is InvMass == 0 on the body,
				// which is the only thing that reads it.
				"Massless",
				Property::fromReadWrite<bool>(
					[](Instance *self) {
						auto *part = self->Cast<BasePart>();
						return part->Physics ? part->Physics->IsMassless(*part) : false;
					},
					[](Instance *self, bool value) {
						auto *part = self->Cast<BasePart>();
						if (part->Physics) part->Physics->SetMassless(*part, value);
					}
				),
			},
			G_COMPONENT_PROPERTY("CFrame", gargantuan::CFrame, Transform.CFrame, ecs::ChangeFlags::Transform),
			G_COMPONENT_PROPERTY("Size", glm::vec3, Transform.Size, ecs::ChangeFlags::Transform),
			G_COMPONENT_PROPERTY("Color", gargantuan::Color3, Visual.Color, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("Transparency", float, Visual.Transparency, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("CastShadow", bool, Visual.CastShadow, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("CanCollide", bool, Collider.CanCollide, ecs::ChangeFlags::Collision),
			G_COMPONENT_PROPERTY("CanQuery", bool, Collider.CanQuery, ecs::ChangeFlags::Collision),
			G_COMPONENT_PROPERTY("CanTouch", bool, Collider.CanTouch, ecs::ChangeFlags::Collision),
			{
				// Reads and writes a uint16 in a sparse set; the string is
				// built only for the script that asked.
				"CollisionGroup",
				Property::fromReadWrite<std::string_view>(
					[](Instance *self) { return self->Cast<BasePart>()->GetCollisionGroup(); },
					[](Instance *self, std::string_view value) { self->Cast<BasePart>()->SetCollisionGroup(value); }
				),
			},
			{
				"Position",
				Property::fromReadWrite<glm::vec3>(
					[](Instance *self) { return self->Cast<BasePart>()->Transform.CFrame.Position; },
					[](Instance *self, glm::vec3 value) {
						auto *part = self->Cast<BasePart>();
						part->Transform.CFrame = gargantuan::CFrame(value, part->Transform.CFrame.Rotation);
						part->MarkChanged(ecs::ChangeFlags::Transform);
					}
				)
					.WithSerializable(false), // a view over CFrame; saving both would conflict
			},
		}
	};

#undef G_COMPONENT_PROPERTY

	bool BasePart::IsAnchored() const {
		return Physics ? Physics->IsAnchored(*this) : DetachedAnchored;
	}

	void BasePart::SetAnchored(bool anchored) {
		if (Physics) {
			Physics->SetAnchored(*this, anchored);
		} else {
			DetachedAnchored = anchored;
		}
	}

	std::string_view BasePart::GetCollisionGroup() const {
		uint16_t id = Physics ? Physics->GetCollisionGroupId(*this) : DetachedCollisionGroup;
		return CollisionGroupTable::GetName(id);
	}

	void BasePart::SetCollisionGroup(std::string_view name) {
		uint16_t id = CollisionGroupTable::GetId(name);
		if (Physics) {
			Physics->SetCollisionGroupId(*this, id);
		} else {
			DetachedCollisionGroup = id;
		}
	}

	glm::mat4 BasePart::GetModelMatrix() const {
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), Transform.CFrame.Position);
		glm::mat4 rotation = Transform.CFrame.Rotation;
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), Transform.Size);
		return translation * rotation * scale;
	}
} // namespace gargantuan
