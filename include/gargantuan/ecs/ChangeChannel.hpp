#pragma once

#include "gargantuan/ecs/ChangeFlags.hpp"

#include <cstdint>
#include <vector>

namespace gargantuan::ecs {
	// May over-report removed rows; consumers discard indexes outside current count.
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
			// The swapped row must be rebuilt; stale `last` entries are filtered later.
			if (removed != last) {
				Mark(removed);
			}
		}

		template <typename Function> void Consume(uint32_t count, Function &&function) {
			for (uint32_t index : Dirty) {
				if (index >= count || !Member[index]) continue;
				Member[index] = 0;
				function(index);
			}
			Dirty.clear();
		}

		void Drain(uint32_t count, std::vector<uint32_t> &out) {
			for (uint32_t index : Dirty) {
				if (index >= count || !Member[index]) continue;
				Member[index] = 0;
				out.push_back(index);
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

	class RegistryBase {
	  public:
		virtual ~RegistryBase() = default;
		virtual void Mark(uint32_t index, ChangeFlags flags) = 0;
	};
}
