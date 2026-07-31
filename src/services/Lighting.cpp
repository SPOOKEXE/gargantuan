#include "gargantuan/services/Lighting.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/ecs/ChangeFlags.hpp"

namespace gargantuan {
	const Lighting::ClassDefinition Lighting::DEFINITION = {
		.Name = "Lighting",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<Lighting>(),
	};

	void Lighting::Attach(WorldRoot *world) {
		World = world;

		Lights.Register(&Rows);
		Lights.Register(&Anchors);
		LightChannel = &Lights.CreateChannel(ecs::ChangeFlags::Visual | ecs::ChangeFlags::Hierarchy);
		PartMotionChannel = &World->Parts.CreateChannel(ecs::ChangeFlags::Transform);

		// Joining or leaving changes which part a light hangs off, so the
		// reverse index has to be rebuilt before it is next read.
		Lights.OnAdded = [this](PointLight *, uint32_t) { AnchorsStale = true; };
		Lights.OnRemoved = [this](PointLight *, uint32_t) { AnchorsStale = true; };

		Lights.Attach(world);
	}

	namespace {
		// A light has no transform of its own; it sits where the part it is
		// parented to sits.
		BasePart *ResolveAnchor(Instance *light) {
			for (Instance *ancestor = light->Parent; ancestor; ancestor = ancestor->Parent) {
				if (auto *part = ancestor->Cast<BasePart>()) {
					return part;
				}
			}
			return nullptr;
		}
	} // namespace

	void Lighting::RebuildAnchors() {
		AnchorsStale = false;
		LightsByPart.clear();

		uint32_t count = Lights.Size();
		for (uint32_t index = 0; index < count; index++) {
			BasePart *anchor = ResolveAnchor(Lights.At(index));
			Anchors[index] = anchor;
			if (anchor) {
				LightsByPart[anchor].push_back(index);
			}
			Rows[index].Position = anchor ? anchor->Transform.CFrame.Position : glm::vec3(0.0f);
		}
	}

	void Lighting::SyncLightRows() {
		if (AnchorsStale) {
			RebuildAnchors();
		}

		uint32_t count = Lights.Size();

		LightChannel->Consume(count, [&](uint32_t index) {
			PointLight *light = Lights.At(index);
			LightRow &row = Rows[index];

			row.Color = (glm::vec3)light->Color;
			row.Range = light->Range;
			// Enabled folded into brightness, so the fill loop reads one word
			// instead of branching back through the instance.
			row.Brightness = light->Enabled ? light->Brightness : 0.0f;

			BasePart *anchor = Anchors[index];
			row.Position = anchor ? anchor->Transform.CFrame.Position : glm::vec3(0.0f);
		});

		// Only the parts that actually moved, and only the lights on them.
		PartMotionChannel->Consume(World->Parts.Size(), [&](uint32_t partIndex) {
			auto it = LightsByPart.find(World->Parts.At(partIndex));
			if (it == LightsByPart.end()) return;

			glm::vec3 position = World->Parts.At(partIndex)->Transform.CFrame.Position;
			for (uint32_t lightIndex : it->second) {
				if (lightIndex < count) {
					Rows[lightIndex].Position = position;
				}
			}
		});
	}
} // namespace gargantuan
