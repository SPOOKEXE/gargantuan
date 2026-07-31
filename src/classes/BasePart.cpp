#include "gargantuan/classes/BasePart.hpp"

#include <unordered_map>
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
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

	// A field of the sparse Surface component. Reading falls back to the
	// default rather than creating an entry, so asking a part about its
	// surface never gives it one.
#define G_SURFACE_PROPERTY(propertyName, valueType, member)                                                            \
	{propertyName,                                                                                                     \
	 Property::fromReadWrite<valueType>(                                                                               \
		 [](Instance *self) -> valueType {                                                                             \
			 const auto *surface = self->Cast<BasePart>()->FindSurface();                                               \
			 return surface ? surface->member : components::Surface{}.member;                                          \
		 },                                                                                                            \
		 [](Instance *self, valueType value) {                                                                         \
			 auto *part = self->Cast<BasePart>();                                                                       \
			 part->EnsureSurface().member = value;                                                                     \
			 part->MarkChanged(ecs::ChangeFlags::Visual);                                                              \
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
				// InvMass == 0 on the body, not a bool on every part.
				"Massless",
				Property::fromReadWrite<bool>(
					[](Instance *self) { return self->Cast<BasePart>()->IsMassless(); },
					[](Instance *self, bool value) { self->Cast<BasePart>()->SetMassless(value); }
				),
			},
			{
				// Studio-only metadata, so it lives in a sparse set with the
				// rest of the things almost no part has.
				"Locked",
				Property::fromReadWrite<bool>(
					[](Instance *self) { return self->Cast<BasePart>()->IsLocked(); },
					[](Instance *self, bool value) { self->Cast<BasePart>()->SetLocked(value); }
				),
			},

			G_COMPONENT_PROPERTY("CFrame", gargantuan::CFrame, Transform.CFrame, ecs::ChangeFlags::Transform),
			G_COMPONENT_PROPERTY("Size", glm::vec3, Transform.Size, ecs::ChangeFlags::Transform),

			G_COMPONENT_PROPERTY("Color", gargantuan::Color3, Visual.Color, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("Transparency", float, Visual.Transparency, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("Reflectance", float, Visual.Reflectance, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("Material", Enums::Material, Visual.Material, ecs::ChangeFlags::Visual),
			G_COMPONENT_PROPERTY("CastShadow", bool, Visual.CastShadow, ecs::ChangeFlags::Visual),

			G_COMPONENT_PROPERTY("CanCollide", bool, Collider.CanCollide, ecs::ChangeFlags::Collision),
			G_COMPONENT_PROPERTY("CanQuery", bool, Collider.CanQuery, ecs::ChangeFlags::Collision),
			G_COMPONENT_PROPERTY("CanTouch", bool, Collider.CanTouch, ecs::ChangeFlags::Collision),

			G_SURFACE_PROPERTY("SurfaceCamera", std::shared_ptr<gargantuan::Camera>, Camera),
			G_SURFACE_PROPERTY("SurfaceImage", std::shared_ptr<gargantuan::EditableImage>, Image),
			G_SURFACE_PROPERTY("SurfaceFace", Enums::NormalId, Face),
			G_SURFACE_PROPERTY("SurfaceTiling", gargantuan::Vector2, Tiling),
			G_SURFACE_PROPERTY("SurfaceOffset", gargantuan::Vector2, Offset),

			{
				// A uint16 in a sparse set; the string is built only for the
				// script that asked.
				"CollisionGroup",
				Property::fromReadWrite<std::string_view>(
					[](Instance *self) { return self->Cast<BasePart>()->GetCollisionGroup(); },
					[](Instance *self, std::string_view value) { self->Cast<BasePart>()->SetCollisionGroup(value); }
				),
			},
			{
				"CustomPhysicalProperties",
				Property::fromReadWrite<std::optional<PhysicalProperties>>(
					[](Instance *self) -> std::optional<PhysicalProperties> {
						auto *part = self->Cast<BasePart>();
						if (!part->World) return std::nullopt;
						const auto *found = part->World->MassOverrides.Find(part->WorldIndex);
						return found ? std::optional<PhysicalProperties>(*found) : std::nullopt;
					},
					[](Instance *self, std::optional<PhysicalProperties> value) {
						auto *part = self->Cast<BasePart>();
						if (value.has_value()) {
							part->SetCustomPhysicalProperties(value.value());
						} else {
							part->ClearCustomPhysicalProperties();
						}
					}
				),
			},
			{
				"Position",
				Property::fromReadWrite<glm::vec3>(
					[](Instance *self) { return self->Cast<BasePart>()->Transform.CFrame.Position; },
					[](Instance *self, glm::vec3 value) {
						auto *part = self->Cast<BasePart>();
						// Written in place. Building a CFrame here copied the
						// rotation out and straight back in for every part a
						// script moves, which on a scene that moves thousands
						// of them a frame is the write itself many times over.
						part->Transform.CFrame.Position = value;
						part->MarkChanged(ecs::ChangeFlags::Transform);
					}
				)
					.WithSerializable(false), // a view over CFrame; saving both would conflict
			},
			{
				"Orientation",
				Property::fromReadWrite<glm::vec3>(
					[](Instance *self) { return self->Cast<BasePart>()->GetOrientation(); },
					[](Instance *self, glm::vec3 value) {
						auto *part = self->Cast<BasePart>();
						part->SetOrientation(value);
						part->MarkChanged(ecs::ChangeFlags::Transform);
					}
				)
					.WithSerializable(false), // also a view over CFrame
			},
			{
				"Mass",
				Property::fromRead([](Instance *self) { return self->Cast<BasePart>()->GetMass(); }),
			},
		}
	};

#undef G_COMPONENT_PROPERTY
#undef G_SURFACE_PROPERTY

	// --- Side table views ---------------------------------------------------

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

	bool BasePart::IsMassless() const {
		return Physics ? Physics->IsMassless(*this) : false;
	}

	void BasePart::SetMassless(bool massless) {
		if (Physics) Physics->SetMassless(*this, massless);
	}

	bool BasePart::IsLocked() const {
		if (!World || WorldIndex == ecs::InvalidIndex) return false;
		const uint8_t *flags = World->EditorFlagBits.Find(WorldIndex);
		return flags && (*flags & EditorFlags::Locked);
	}

	void BasePart::SetLocked(bool locked) {
		if (!World || WorldIndex == ecs::InvalidIndex) return;
		if (locked) {
			World->EditorFlagBits.Add(WorldIndex, EditorFlags::Locked);
		} else {
			World->EditorFlagBits.Remove(WorldIndex);
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

	const components::Surface *BasePart::FindSurface() const {
		if (!World || WorldIndex == ecs::InvalidIndex) return nullptr;
		return World->Surfaces.Find(WorldIndex);
	}

	const components::Surface &BasePart::GetSurfaceOrDefault() const {
		static const components::Surface none;
		const components::Surface *found = FindSurface();
		return found ? *found : none;
	}

	namespace {
		// Only ever holds parts that were given a surface while detached, which is
		// a handful during a build and none at rest. A map rather than a field, so
		// no part pays for this.
		std::unordered_map<const BasePart *, components::Surface> &PendingSurfaces() {
			static std::unordered_map<const BasePart *, components::Surface> pending;
			return pending;
		}
	} // namespace

	BasePart::~BasePart() {
		// Erased here as well as on flush: a part destroyed before it was ever
		// parented would otherwise leave an entry keyed on an address that a
		// later part could be allocated at.
		if (!PendingSurfaces().empty()) {
			PendingSurfaces().erase(this);
		}
	}

	void BasePart::FlushPendingSurface() {
		if (!World || WorldIndex == ecs::InvalidIndex) return;

		auto &pending = PendingSurfaces();
		auto found = pending.find(this);
		if (found == pending.end()) return;

		World->Surfaces.Add(WorldIndex, found->second);
		pending.erase(found);
		MarkChanged(ecs::ChangeFlags::Visual);
	}

	components::Surface &BasePart::EnsureSurface() {
		// A part with no world has nowhere to put one yet, so the write goes to a
		// per-part holding entry that FlushPendingSurface moves in on parenting.
		if (!World || WorldIndex == ecs::InvalidIndex) return PendingSurfaces()[this];

		if (auto *existing = World->Surfaces.Find(WorldIndex)) return *existing;
		return World->Surfaces.Add(WorldIndex, components::Surface{});
	}

	PhysicalProperties BasePart::GetPhysicalProperties() const {
		if (World && WorldIndex != ecs::InvalidIndex) {
			if (const auto *found = World->MassOverrides.Find(WorldIndex)) return *found;
		}
		return PhysicalProperties(Visual.Material);
	}

	void BasePart::SetCustomPhysicalProperties(PhysicalProperties properties) {
		if (!World || WorldIndex == ecs::InvalidIndex) return;
		World->MassOverrides.Add(WorldIndex, properties);
		MarkChanged(ecs::ChangeFlags::Physics);
	}

	void BasePart::ClearCustomPhysicalProperties() {
		if (!World || WorldIndex == ecs::InvalidIndex) return;
		World->MassOverrides.Remove(WorldIndex);
		MarkChanged(ecs::ChangeFlags::Physics);
	}

	// --- Derived ------------------------------------------------------------

	glm::vec4 BasePart::GetSurfaceMatch() const {
		return SurfaceMatchOf(GetSurfaceOrDefault().Face);
	}

	glm::vec4 BasePart::SurfaceMatchOf(Enums::NormalId face) {
		switch (face) {
		case Enums::NormalId::Right: return {1, 0, 0, SURFACE_MATCH_NORMAL};
		case Enums::NormalId::Top: return {0, 1, 0, SURFACE_MATCH_NORMAL};
		case Enums::NormalId::Back: return {0, 0, 1, SURFACE_MATCH_NORMAL};
		case Enums::NormalId::Left: return {-1, 0, 0, SURFACE_MATCH_NORMAL};
		case Enums::NormalId::Bottom: return {0, -1, 0, SURFACE_MATCH_NORMAL};

		// The wedge's slope, which leans along Y and Z only. Must stay the
		// vector PrimitiveMeshes gives the slope's own vertices, or this
		// compares against a direction the face does not point in.
		case Enums::NormalId::Slope: return {0.0f, 0.70710678f, 0.70710678f, SURFACE_MATCH_NORMAL};

		// A ball is one face that faces everywhere
		case Enums::NormalId::Sphere: return {0, 0, 0, SURFACE_MATCH_ANY};

		// The curved side of a cylinder, which is everything square to the
		// axis its flat ends sit on. Roblox puts those ends on Right and Left,
		// so the axis is X.
		case Enums::NormalId::Circumference: return {1, 0, 0, SURFACE_MATCH_AROUND};

		case Enums::NormalId::Front:
		default: return {0, 0, -1, SURFACE_MATCH_NORMAL};
		}
	}

	glm::vec3 BasePart::GetOrientation() const {
		auto [x, y, z] = Transform.CFrame.ToOrientation();
		return glm::degrees(glm::vec3((float)x, (float)y, (float)z));
	}

	void BasePart::SetOrientation(glm::vec3 orientation) {
		glm::vec3 radians = glm::radians(orientation);
		Transform.CFrame = gargantuan::CFrame(
			Transform.CFrame.Position, gargantuan::CFrame::Angles(radians.x, radians.y, radians.z).Rotation
		);
	}

	float BasePart::GetMass() const {
		float volume = Transform.Size.x * Transform.Size.y * Transform.Size.z;
		return volume * GetPhysicalProperties().Density;
	}

	glm::mat4 BasePart::GetModelMatrix() const {
		// Written out rather than composed from three matrices and two full
		// multiplies. Translate times rotate times scale has a closed form: the
		// scale only ever multiplies its own column, and the translation only
		// ever lands in the last one, so of the hundred and twenty eight
		// multiply-adds a pair of 4x4 products costs, nine do the work and the
		// rest are against the zeroes and ones of two nearly empty matrices.
		//
		//   T * R * S  =  [ r0*sx  r1*sy  r2*sz  position ]
		//
		// This is the hottest arithmetic in the engine -- every pass asks every
		// part for it, every frame -- so it is worth writing once and reading
		// twice.
		glm::mat4 model;
		model[0] = glm::vec4(Transform.CFrame.Rotation[0] * Transform.Size.x, 0.0f);
		model[1] = glm::vec4(Transform.CFrame.Rotation[1] * Transform.Size.y, 0.0f);
		model[2] = glm::vec4(Transform.CFrame.Rotation[2] * Transform.Size.z, 0.0f);
		model[3] = glm::vec4(Transform.CFrame.Position, 1.0f);
		return model;
	}
} // namespace gargantuan
