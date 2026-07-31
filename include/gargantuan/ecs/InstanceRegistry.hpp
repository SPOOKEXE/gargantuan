#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace gargantuan::ecs {
	// Tracks every descendant of a root that is a T, keeps them in a dense
	// array, and keeps any component storage registered against it in step.
	//
	// This is the piece WorldRoot used to hand-roll. Hooking DescendantAdded
	// rather than ChildAdded is the important part: a part nested inside a model
	// belongs to the world just as much as a direct child does.
	template <typename T> class InstanceRegistry final : public RegistryBase {
	  public:
		// Called after a row is appended and before it is removed, so a world
		// can wire up whatever else the entity needs while its index is valid.
		std::function<void(T *, uint32_t)> OnAdded;
		std::function<void(T *, uint32_t)> OnRemoved;

		void Attach(Instance *root) {
			root->GetDescendantAdded()->Connect([this](Instance::Pointer instance) { Add(instance); });
			root->GetDescendantRemoved()->Connect([this](Instance::Pointer instance) { Remove(instance); });

			// Anything parented before the hooks went in.
			for (auto &descendant : root->GetDescendants()) {
				Add(descendant);
			}
		}

		// Component storage is registered up front, before any entity joins.
		// Registering late is supported but replays every existing row.
		void Register(ComponentSetBase *set) {
			Columns.push_back(set);
			for (uint32_t index = 0; index < Rows.size(); index++) {
				set->OnAdd(index);
			}
		}

		ChangeChannel &CreateChannel(ChangeFlags mask) {
			Channels.emplace_back(mask);
			ChangeChannel &channel = Channels.back();
			channel.MarkAll((uint32_t)Rows.size());
			return channel;
		}

		void Mark(uint32_t index, ChangeFlags flags) override {
			if (index >= Rows.size()) return;
			for (auto &channel : Channels) {
				if (Overlaps(channel.Mask, flags)) {
					channel.Mark(index);
				}
			}
		}

		std::span<T *const> Raw() const {
			return Rows;
		}
		uint32_t Size() const {
			return (uint32_t)Rows.size();
		}
		T *At(uint32_t index) const {
			return Rows[index];
		}

		auto begin() const {
			return Rows.begin();
		}
		auto end() const {
			return Rows.end();
		}

	  private:
		void Add(Instance::Pointer instance) {
			if (!instance || !instance->IsA<T>()) return;
			if (instance->Registry == this) return;

			uint32_t index = (uint32_t)Rows.size();
			instance->WorldIndex = index;
			instance->Registry = this;

			auto typed = std::static_pointer_cast<T>(instance);
			Owned.push_back(typed);
			Rows.push_back(typed.get());

			for (auto *column : Columns) {
				column->OnAdd(index);
			}
			for (auto &channel : Channels) {
				channel.OnAdd(index);
			}

			if (OnAdded) OnAdded(typed.get(), index);
		}

		void Remove(Instance::Pointer instance) {
			if (!instance || instance->Registry != this) return;

			uint32_t index = instance->WorldIndex;
			if (index >= Rows.size() || Rows[index] != instance.get()) return;
			uint32_t last = (uint32_t)Rows.size() - 1;

			if (OnRemoved) OnRemoved(Rows[index], index);

			for (auto *column : Columns) {
				column->OnSwapRemove(index, last);
			}
			for (auto &channel : Channels) {
				channel.OnSwapRemove(index, last);
			}

			if (index != last) {
				Rows[index] = Rows[last];
				Owned[index] = std::move(Owned[last]);
				Rows[index]->WorldIndex = index;
			}
			Rows.pop_back();
			Owned.pop_back();

			instance->WorldIndex = InvalidIndex;
			instance->Registry = nullptr;
		}

		std::vector<std::shared_ptr<T>> Owned; // keeps the instances alive
		std::vector<T *> Rows;                 // what systems walk
		std::vector<ComponentSetBase *> Columns;
		std::deque<ChangeChannel> Channels; // deque: subscribers hold references
	};
} // namespace gargantuan::ecs
