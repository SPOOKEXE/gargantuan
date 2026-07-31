#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/ecs/ChangeChannel.hpp"
#include "gargantuan/ecs/ComponentSet.hpp"

#include <array>
#include <bit>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace gargantuan::ecs {
	template <typename T> class InstanceRegistry final : public RegistryBase {
	  public:
		std::function<void(T *, uint32_t)> OnAdded;
		std::function<void(T *, uint32_t)> OnRemoved;

		void Attach(Instance *root) {
			root->GetDescendantAdded()->Connect([this](Instance::Pointer instance) { Add(instance); });
			root->GetDescendantRemoved()->Connect([this](Instance::Pointer instance) { Remove(instance); });

			for (auto &descendant : root->GetDescendants()) {
				Add(descendant);
			}
		}

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

			for (int bit = 0; bit < 8; bit++) {
				if (Overlaps(mask, (ChangeFlags)(1u << bit))) {
					ChannelsByFlag[bit].push_back(&channel);
				}
			}
			return channel;
		}

		void Mark(uint32_t index, ChangeFlags flags) override {
			if (index >= Rows.size()) return;

			uint8_t bits = (uint8_t)flags;
			while (bits) {
				int bit = std::countr_zero(bits);
				bits &= (uint8_t)(bits - 1);
				for (ChangeChannel *channel : ChannelsByFlag[bit]) {
					channel->Mark(index);
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

		std::vector<std::shared_ptr<T>> Owned;
		std::vector<T *> Rows;
		std::vector<ComponentSetBase *> Columns;
		std::deque<ChangeChannel> Channels; // Subscribers retain channel references.
		std::array<std::vector<ChangeChannel *>, 8> ChannelsByFlag;
	};
}
