#pragma once

#include "gargantuan/ecs/ChangeFlags.hpp"

#include <cstdint>
#include <vector>

namespace gargantuan::ecs {
	// A per-consumer dirty list. Systems that cache derived rows subscribe to
	// one of these instead of rescanning every entity, and each subscriber gets
	// its own so a write only wakes the systems that read that data.
	//
	// The list is allowed to over-report: entries left over from a removed row
	// are skipped at consume time rather than being erased eagerly.
	class ChangeChannel {
	  public:
		explicit ChangeChannel(ChangeFlags mask) : Mask(mask) {}

		ChangeFlags Mask;

		void Mark(uint32_t index) {
			if (Member.size() <= index) {
				Member.resize(index + 1, 0);
			}
			if (Member[index]) return;
			Member[index] = 1;
			Dirty.push_back(index);
		}

		void MarkAll(uint32_t count) {
			for (uint32_t index = 0; index < count; index++) {
				Mark(index);
			}
		}

		void OnAdd(uint32_t index) {
			if (Member.size() <= index) {
				Member.resize(index + 1, 0);
			}
			Member[index] = 0;
			Mark(index);
		}

		void OnSwapRemove(uint32_t removed, uint32_t last) {
			Member[removed] = 0;
			Member.pop_back();
			// Whatever moved into `removed` needs its row rebuilt. Stale Dirty
			// entries pointing at `last` are now out of range and get skipped.
			if (removed != last) {
				Mark(removed);
			}
		}

		// Visits each dirty row once and clears it. `count` is the registry's
		// current row count; anything past it no longer exists.
		template <typename Function> void Consume(uint32_t count, Function &&function) {
			for (uint32_t index : Dirty) {
				if (index >= count || !Member[index]) continue;
				Member[index] = 0;
				function(index);
			}
			Dirty.clear();
		}

		bool Empty() const {
			return Dirty.empty();
		}

	  private:
		std::vector<uint32_t> Dirty;
		std::vector<uint8_t> Member;
	};

	// Non-template face of InstanceRegistry, so an Instance can report a change
	// without knowing which registry it joined.
	class RegistryBase {
	  public:
		virtual ~RegistryBase() = default;
		virtual void Mark(uint32_t index, ChangeFlags flags) = 0;
	};
} // namespace gargantuan::ecs
