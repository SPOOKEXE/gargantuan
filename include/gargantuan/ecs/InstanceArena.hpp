#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace gargantuan::ecs {
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
		bool Enabled = false;
		size_t BlockSize = 0;
		size_t Used = 0;

		std::vector<std::unique_ptr<std::byte[]>> Chunks;
		FreeBlock *FreeList = nullptr;
	};

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
}
