#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/PointLight.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"
#include "gargantuan/ecs/InstanceRegistry.hpp"
#include "gargantuan/render/LightRow.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	// The whole class, storage included. Everything that used to have to be
	// written out per class -- the owning vector, the raw row vector, the
	// swap-remove, the back-index fixup, the change list -- comes from the
	// registry, and what is left is the data and one Attach call.
	class Lighting : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		ecs::InstanceRegistry<PointLight> Lights;

		// Services are constructed before the world exists, so the root is
		// handed over once the tree is up rather than found in the constructor.
		void Attach(WorldRoot *world);

		std::span<const LightRow> GetLightRows() const {
			return Rows.Values();
		}

		void SyncLightRows();

	  private:
		void RebuildAnchors();

		ecs::Column<LightRow> Rows;
		// The part each light hangs off, as a pointer rather than a row index:
		// a part's index moves when the world swap-removes, its address does
		// not. Kept in step with the light rows by the column itself.
		ecs::Column<BasePart *> Anchors;

		// Reverse index, so a part that moved can mark just the lights on it
		// instead of every light being re-resolved each frame.
		std::unordered_map<const BasePart *, std::vector<uint32_t>> LightsByPart;
		bool AnchorsStale = true;

		ecs::ChangeChannel *LightChannel = nullptr;
		// A channel on the *parts* registry: light positions are derived from
		// part transforms, so a part moving has to reach the lights on it.
		ecs::ChangeChannel *PartMotionChannel = nullptr;

		WorldRoot *World = nullptr;
	};
} // namespace gargantuan
