#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gargantuan::ecs {
	inline constexpr uint32_t InvalidIndex = UINT32_MAX;

	// Every component store keyed by an entity's row index implements this, so
	// the registry can keep them in step when it swap-removes a row.
	//
	// The registry only ever appends or swap-removes, so two hooks cover it.
	class ComponentSetBase {
	  public:
		virtual ~ComponentSetBase() = default;

		virtual void OnAdd(uint32_t index) = 0;
		virtual void OnSwapRemove(uint32_t removed, uint32_t last) = 0;
	};

	// Dense storage: one T per entity, addressed by row index. Use when nearly
	// every entity in the family has the component and a system walks all of
	// them.
	template <typename T> class Column final : public ComponentSetBase {
	  public:
		void OnAdd(uint32_t index) override {
			if (Data.size() <= index) {
				Data.resize(index + 1);
			}
			Data[index] = T{};
		}

		void OnSwapRemove(uint32_t removed, uint32_t last) override {
			if (removed != last) {
				Data[removed] = std::move(Data[last]);
			}
			Data.pop_back();
		}

		T &operator[](uint32_t index) {
			return Data[index];
		}
		const T &operator[](uint32_t index) const {
			return Data[index];
		}

		std::span<T> Values() {
			return Data;
		}
		std::span<const T> Values() const {
			return Data;
		}

		size_t Size() const {
			return Data.size();
		}

		auto begin() {
			return Data.begin();
		}
		auto end() {
			return Data.end();
		}

	  private:
		std::vector<T> Data;
	};

	// Sparse storage: only the entities that actually have the component, held
	// contiguously so a system iterating them touches nothing else.
	//
	// Membership is meaningful on its own -- Anchored is the absence of a
	// RigidBody, not a bool on every part.
	template <typename T> class SparseSet final : public ComponentSetBase {
	  public:
		void OnAdd(uint32_t index) override {
			if (Sparse.size() <= index) {
				Sparse.resize(index + 1, InvalidIndex);
			}
			Sparse[index] = InvalidIndex;
		}

		void OnSwapRemove(uint32_t removed, uint32_t last) override {
			Remove(removed);
			if (removed != last) {
				// The entity that was at `last` now lives at `removed`; rekey
				// its component rather than dropping it.
				if (uint32_t slot = Sparse[last]; slot != InvalidIndex) {
					Keys[slot] = removed;
					Sparse[removed] = slot;
					Sparse[last] = InvalidIndex;
				}
			}
			Sparse.pop_back();
		}

		bool Has(uint32_t index) const {
			return index < Sparse.size() && Sparse[index] != InvalidIndex;
		}

		T *Find(uint32_t index) {
			if (!Has(index)) return nullptr;
			return &Data[Sparse[index]];
		}

		const T *Find(uint32_t index) const {
			if (!Has(index)) return nullptr;
			return &Data[Sparse[index]];
		}

		T &Add(uint32_t index, T value = T{}) {
			if (Sparse.size() <= index) {
				Sparse.resize(index + 1, InvalidIndex);
			}
			if (uint32_t slot = Sparse[index]; slot != InvalidIndex) {
				Data[slot] = std::move(value);
				return Data[slot];
			}
			Sparse[index] = (uint32_t)Data.size();
			Keys.push_back(index);
			Data.push_back(std::move(value));
			return Data.back();
		}

		void Remove(uint32_t index) {
			if (!Has(index)) return;

			uint32_t slot = Sparse[index];
			uint32_t lastSlot = (uint32_t)Data.size() - 1;
			if (slot != lastSlot) {
				Data[slot] = std::move(Data[lastSlot]);
				Keys[slot] = Keys[lastSlot];
				Sparse[Keys[slot]] = slot;
			}
			Data.pop_back();
			Keys.pop_back();
			Sparse[index] = InvalidIndex;
		}

		size_t Size() const {
			return Data.size();
		}

		// Parallel arrays: Keys()[i] is the entity row index owning Values()[i].
		std::span<const uint32_t> EntityKeys() const {
			return Keys;
		}
		std::span<T> Values() {
			return Data;
		}
		std::span<const T> Values() const {
			return Data;
		}

	  private:
		std::vector<uint32_t> Sparse; // row index -> slot in Data, or InvalidIndex
		std::vector<uint32_t> Keys;   // slot -> row index
		std::vector<T> Data;
	};
} // namespace gargantuan::ecs
