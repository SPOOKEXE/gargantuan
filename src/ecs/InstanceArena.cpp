#include "gargantuan/ecs/InstanceArena.hpp"

#include <cstddef>

namespace gargantuan::ecs {
	void InstanceArena::Configure(size_t size, size_t alignment) {
		Configured = true;

		// new[] only guarantees the default new alignment. A class that wants
		// more than that is rare enough to not be worth a hand-aligned chunk,
		// so the arena bows out and everything falls through to ::operator new.
		if (alignment > alignof(std::max_align_t)) {
			Enabled = false;
			return;
		}

		Enabled = true;
		BlockSize = size < sizeof(FreeBlock) ? sizeof(FreeBlock) : size;
		// Round up so every block in a chunk starts aligned.
		BlockSize = (BlockSize + alignment - 1) / alignment * alignment;
	}

	void *InstanceArena::Allocate(size_t size, size_t alignment) {
		if (!Configured) Configure(size, alignment);

		if (!Enabled || size > BlockSize || alignment > alignof(std::max_align_t)) {
			return ::operator new(size, std::align_val_t(alignment));
		}

		if (FreeList) {
			void *block = FreeList;
			FreeList = FreeList->Next;
			return block;
		}

		if (Chunks.empty() || Used == BlocksPerChunk) {
			Chunks.push_back(std::make_unique<std::byte[]>(BlockSize * BlocksPerChunk));
			Used = 0;
		}

		void *block = Chunks.back().get() + Used * BlockSize;
		Used++;
		return block;
	}

	void InstanceArena::Free(void *pointer, size_t size, size_t alignment) {
		if (!pointer) return;

		if (!Enabled || size > BlockSize || alignment > alignof(std::max_align_t)) {
			::operator delete(pointer, std::align_val_t(alignment));
			return;
		}

		// Chunks are kept, not returned: a place churning instances reuses the
		// blocks, and handing pages back only to ask for them again is churn
		// for its own sake.
		auto *block = (FreeBlock *)pointer;
		block->Next = FreeList;
		FreeList = block;
	}
} // namespace gargantuan::ecs
