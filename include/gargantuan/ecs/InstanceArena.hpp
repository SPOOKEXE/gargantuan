#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace gargantuan::ecs {
	// A fixed-block pool, one per class.
	//
	// Every instance of a class allocates exactly the same number of bytes --
	// the object and its shared_ptr control block together, since instances are
	// created with allocate_shared -- so a single block size covers the class
	// for the life of the process.
	//
	// The point is not that a bump pointer beats malloc on its own, it is that
	// instances of a class land on shared pages. Walking a registry's rows then
	// reads sequential memory instead of chasing pointers through wherever the
	// general allocator happened to put each one.
	class InstanceArena {
	  public:
		static constexpr size_t BlocksPerChunk = 256;

		void *Allocate(size_t size, size_t alignment);
		void Free(void *pointer, size_t size, size_t alignment);

		size_t GetChunkCount() const {
			return Chunks.size();
		}
		size_t GetBlockSize() const {
			return BlockSize;
		}

	  private:
		struct FreeBlock {
			FreeBlock *Next;
		};

		void Configure(size_t size, size_t alignment);

		bool Configured = false;
		// Off when the class wants over-aligned storage, in which case every
		// request falls through to the general allocator.
		bool Enabled = false;
		size_t BlockSize = 0;
		size_t Used = 0; // blocks handed out of the newest chunk

		std::vector<std::unique_ptr<std::byte[]>> Chunks;
		FreeBlock *FreeList = nullptr;
	};

	// Stateful allocator over an InstanceArena. The arena outlives every
	// instance: it hangs off the ClassDefinition in the registry, which is
	// created once and never torn down.
	template <typename T> struct InstanceAllocator {
		using value_type = T;

		InstanceArena *Arena = nullptr;

		InstanceAllocator() = default;
		explicit InstanceAllocator(InstanceArena *arena) : Arena(arena) {}
		template <typename U> InstanceAllocator(const InstanceAllocator<U> &other) : Arena(other.Arena) {}

		T *allocate(size_t count) {
			if (Arena && count == 1) {
				return (T *)Arena->Allocate(sizeof(T), alignof(T));
			}
			return (T *)::operator new(count * sizeof(T), std::align_val_t(alignof(T)));
		}

		void deallocate(T *pointer, size_t count) {
			if (Arena && count == 1) {
				Arena->Free(pointer, sizeof(T), alignof(T));
				return;
			}
			::operator delete(pointer, std::align_val_t(alignof(T)));
		}

		template <typename U> bool operator==(const InstanceAllocator<U> &other) const {
			return Arena == other.Arena;
		}
		template <typename U> bool operator!=(const InstanceAllocator<U> &other) const {
			return Arena != other.Arena;
		}
	};
} // namespace gargantuan::ecs
