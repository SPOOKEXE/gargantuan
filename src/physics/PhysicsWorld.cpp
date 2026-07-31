#include "gargantuan/physics/PhysicsWorld.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"

#include <box3d/box3d.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	namespace CollisionGroupTable {
		namespace {
			struct Table {
				std::vector<std::string> Names{"Default"};
				std::unordered_map<std::string_view, uint16_t> Ids{{"Default", DefaultId}};
			};

			Table &Get() {
				static Table *table = new Table();
				return *table;
			}
		} // namespace

		uint16_t GetId(std::string_view name) {
			Table &table = Get();
			if (auto it = table.Ids.find(name); it != table.Ids.end()) {
				return it->second;
			}

			auto id = (uint16_t)table.Names.size();
			// The string is owned by the table, and the key views into it, so
			// the view stays valid for the life of the process.
			table.Names.emplace_back(name);
			table.Ids.emplace(table.Names.back(), id);
			return id;
		}

		std::string_view GetName(uint16_t id) {
			Table &table = Get();
			return id < table.Names.size() ? std::string_view(table.Names[id]) : std::string_view("Default");
		}
	} // namespace CollisionGroupTable

	PhysicsWorld::~PhysicsWorld() {
		if (b3World_IsValid(Solver)) {
			b3DestroyWorld(Solver);
			Solver = b3_nullWorldId;
		}
	}

	void PhysicsWorld::EnsureSolver() {
		if (b3World_IsValid(Solver)) return;

		b3WorldDef definition = b3DefaultWorldDef();
		definition.gravity = {Gravity.x, Gravity.y, Gravity.z};
		Solver = b3CreateWorld(&definition);
	}

	void PhysicsWorld::SetGravity(glm::vec3 gravity) {
		Gravity = gravity;
		if (b3World_IsValid(Solver)) {
			b3World_SetGravity(Solver, {gravity.x, gravity.y, gravity.z});
		}
	}

	void PhysicsWorld::CreateBody(const BasePart &part, RigidBody &body) {
		EnsureSolver();

		const auto &frame = part.Transform.CFrame;
		glm::quat rotation = glm::quat_cast(glm::mat3(frame.Rotation));

		b3BodyDef definition = b3DefaultBodyDef();
		definition.type = b3_dynamicBody;
		definition.position = {frame.Position.x, frame.Position.y, frame.Position.z};
		definition.rotation = {{rotation.x, rotation.y, rotation.z}, rotation.w};
		definition.linearVelocity = {body.Velocity.x, body.Velocity.y, body.Velocity.z};

		// No shapes attached. Nothing collides with anything until shapes are
		// added, which is a deliberate default rather than an oversight:
		// turning collision on would move parts in scenes authored without a
		// solver running. Mass comes from the component instead of from shape
		// volume, which is what makes a shapeless body integrate at all.
		definition.enableSleep = false;
		body.Body = b3CreateBody(Solver, &definition);
		ApplyMassData(body);
	}

	// The component owns the mass properties, so they are pushed into the
	// solver rather than derived from geometry. Massless is InvMass == 0, and
	// box3d reads that as a body gravity does not act on.
	void PhysicsWorld::ApplyMassData(RigidBody &body) {
		if (!b3Body_IsValid(body.Body)) return;

		b3MassData mass{};
		mass.mass = body.InvMass > 0.0f ? 1.0f / body.InvMass : 0.0f;
		mass.center = {0.0f, 0.0f, 0.0f};
		mass.inertia = {
			{body.InvInertia.x > 0.0f ? 1.0f / body.InvInertia.x : 0.0f, 0.0f, 0.0f},
			{0.0f, body.InvInertia.y > 0.0f ? 1.0f / body.InvInertia.y : 0.0f, 0.0f},
			{0.0f, 0.0f, body.InvInertia.z > 0.0f ? 1.0f / body.InvInertia.z : 0.0f},
		};
		b3Body_SetMassData(body.Body, mass);
	}

	bool PhysicsWorld::IsMassless(const BasePart &part) const {
		const RigidBody *body = part.WorldIndex == ecs::InvalidIndex ? nullptr : Bodies.Find(part.WorldIndex);
		return body ? body->InvMass == 0.0f : false;
	}

	void PhysicsWorld::SetMassless(BasePart &part, bool massless) {
		RigidBody *body = part.WorldIndex == ecs::InvalidIndex ? nullptr : Bodies.Find(part.WorldIndex);
		if (!body) return;

		body->InvMass = massless ? 0.0f : 1.0f;
		ApplyMassData(*body);
		part.MarkChanged(ecs::ChangeFlags::Physics);
	}

	void PhysicsWorld::DestroyBody(RigidBody &body) {
		if (b3Body_IsValid(body.Body)) {
			b3DestroyBody(body.Body);
		}
		body.Body = b3_nullBodyId;
	}

	bool PhysicsWorld::IsAnchored(const BasePart &part) const {
		if (part.WorldIndex == ecs::InvalidIndex) return part.DetachedAnchored;
		return !Bodies.Has(part.WorldIndex);
	}

	void PhysicsWorld::SetAnchored(BasePart &part, bool anchored) {
		if (part.WorldIndex == ecs::InvalidIndex) {
			part.DetachedAnchored = anchored;
			return;
		}

		if (anchored) {
			if (RigidBody *body = Bodies.Find(part.WorldIndex)) {
				DestroyBody(*body);
			}
			Bodies.Remove(part.WorldIndex);
		} else if (!Bodies.Has(part.WorldIndex)) {
			CreateBody(part, Bodies.Add(part.WorldIndex, RigidBody{}));
		}

		part.MarkChanged(ecs::ChangeFlags::Physics);
	}

	uint16_t PhysicsWorld::GetCollisionGroupId(const BasePart &part) const {
		if (part.WorldIndex == ecs::InvalidIndex) return part.DetachedCollisionGroup;
		const uint16_t *id = CollisionGroups.Find(part.WorldIndex);
		return id ? *id : CollisionGroupTable::DefaultId;
	}

	void PhysicsWorld::SetCollisionGroupId(BasePart &part, uint16_t id) {
		if (part.WorldIndex == ecs::InvalidIndex) {
			part.DetachedCollisionGroup = id;
			return;
		}

		// The default group is the absent case, so setting it back removes the
		// entry rather than storing a zero on every part in the world.
		if (id == CollisionGroupTable::DefaultId) {
			CollisionGroups.Remove(part.WorldIndex);
		} else {
			CollisionGroups.Add(part.WorldIndex, id);
		}

		part.MarkChanged(ecs::ChangeFlags::Collision);
	}

	void PhysicsWorld::OnPartAdded(BasePart &part) {
		part.Physics = this;

		// The part carried these in a couple of fields while it had no world;
		// turn them back into presence or absence in the sets.
		if (!part.DetachedAnchored && !Bodies.Has(part.WorldIndex)) {
			CreateBody(part, Bodies.Add(part.WorldIndex, RigidBody{}));
		}
		if (part.DetachedCollisionGroup != CollisionGroupTable::DefaultId) {
			CollisionGroups.Add(part.WorldIndex, part.DetachedCollisionGroup);
		}
	}

	void PhysicsWorld::OnPartRemoved(BasePart &part) {
		part.DetachedAnchored = !Bodies.Has(part.WorldIndex);
		// The row is about to be swapped out from under the set, so the solver
		// body has to go with it.
		if (RigidBody *body = Bodies.Find(part.WorldIndex)) {
			DestroyBody(*body);
		}
		const uint16_t *group = CollisionGroups.Find(part.WorldIndex);
		part.DetachedCollisionGroup = group ? *group : CollisionGroupTable::DefaultId;
		part.Physics = nullptr;
	}

	void PhysicsWorld::Step(ecs::InstanceRegistry<BasePart> &parts, float deltaTime, ecs::ChangeChannel &solverChannel) {
		if (!b3World_IsValid(Solver) || deltaTime <= 0.0f) {
			solverChannel.Consume(parts.Size(), [](uint32_t) {});
			return;
		}

		// Transforms authored outside the solver win. Only the parts that were
		// actually written are pushed back in.
		solverChannel.Consume(parts.Size(), [&](uint32_t index) {
			RigidBody *body = Bodies.Find(index);
			if (!body || !b3Body_IsValid(body->Body)) return;

			const auto &frame = parts.At(index)->Transform.CFrame;
			glm::quat rotation = glm::quat_cast(glm::mat3(frame.Rotation));
			b3Body_SetTransform(
				body->Body,
				{frame.Position.x, frame.Position.y, frame.Position.z},
				{{rotation.x, rotation.y, rotation.z}, rotation.w}
			);
		});

		b3World_Step(Solver, deltaTime, SubStepCount);
		// Read back exactly the movers, contiguous. Anchored parts are not in
		// this array to begin with, so there is no branch to skip them.
		auto keys = Bodies.EntityKeys();
		auto bodies = Bodies.Values();

		for (size_t slot = 0; slot < bodies.size(); slot++) {
			RigidBody &body = bodies[slot];
			uint32_t index = keys[slot];
			if (index >= parts.Size() || !b3Body_IsValid(body.Body)) continue;

			b3Vec3 velocity = b3Body_GetLinearVelocity(body.Body);
			body.Velocity = {velocity.x, velocity.y, velocity.z};

			b3Pos position = b3Body_GetPosition(body.Body);
			auto next = glm::vec3((float)position.x, (float)position.y, (float)position.z);

			BasePart *part = parts.At(index);
			auto &frame = part->Transform.CFrame;
			if (next == frame.Position) continue;

			b3Quat rotation = b3Body_GetRotation(body.Body);
			frame = gargantuan::CFrame(
				next, glm::mat4_cast(glm::quat(rotation.s, rotation.v.x, rotation.v.y, rotation.v.z))
			);
			part->MarkChanged(ecs::ChangeFlags::Transform);
		}
	}

	void PhysicsWorld::SyncBroadphase(ecs::InstanceRegistry<BasePart> &parts, ecs::ChangeChannel &channel) {
		uint32_t count = parts.Size();
		channel.Consume(count, [&](uint32_t index) {
			BasePart *part = parts.At(index);
			BroadphaseRow &row = Broadphase[index];

			// Conservative AABB: the rotated box never leaves the sphere that
			// contains it, and a sweep is allowed to over-report.
			float radius = glm::length(part->Transform.Size) * 0.5f;
			glm::vec3 centre = part->Transform.CFrame.Position;
			row.Min = centre - glm::vec3(radius);
			row.Max = centre + glm::vec3(radius);

			row.Flags = 0;
			if (part->Collider.CanCollide) row.Flags |= ColliderFlags::CanCollide;
			if (part->Collider.CanQuery) row.Flags |= ColliderFlags::CanQuery;
			if (part->Collider.CanTouch) row.Flags |= ColliderFlags::CanTouch;

			const uint16_t *group = CollisionGroups.Find(index);
			row.CollisionGroupId = group ? *group : CollisionGroupTable::DefaultId;
		});
	}
} // namespace gargantuan
