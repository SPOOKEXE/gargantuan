#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gargantuan {
	namespace FrameGraph {
		inline constexpr uint32_t MaxDepth = 12;

		inline constexpr size_t MaxSamples = 4096;

		inline constexpr uint32_t NoParent = UINT32_MAX;

		struct Sample {
			std::string_view Name;
			uint32_t Depth = 0;
			uint32_t Parent = NoParent;
			float StartMilliseconds = 0.0f;
			float Milliseconds = 0.0f;
		};

		struct Counter {
			const char *Name = nullptr;
			double Value = 0.0;
			uint32_t Samples = 0;
			bool IsTime = false;
		};

		void RecordCounter(const char *name, double value, bool isTime);
		const std::vector<Counter> &GetCounters();

		void BeginFrame();
		void EndFrame();

		void SetEnabled(bool enabled);
		bool IsEnabled();

		const std::vector<Sample> &GetSamples();
		size_t GetDropped();
		float GetFrameMilliseconds();

		inline constexpr size_t RecentFrames = 300;

		float GetRecentMax(std::string_view name);

		// Mean absolute change in frame time across the recent window: stutter,
		// which the average cannot show because a steady slow frame has the same one.
		float GetFrameJitter();

		inline constexpr double HistorySeconds = 5.0;
		inline constexpr size_t MaxHistoryFrames = 20000;
		inline constexpr size_t MaxHistoryNames = 256;

		bool WriteSnapshot(const char *path);
		size_t GetHistoryFrames();
		double GetHistorySeconds();

		void Push(std::string_view name);
		void PushCopied(std::string_view name);
		void Pop();

		struct Scope {
			explicit Scope(std::string_view name) {
				if (IsEnabled()) {
					Push(name);
					Recorded = true;
				}
			}
			~Scope() {
				if (Recorded) {
					Pop();
				}
			}

			Scope(const Scope &) = delete;
			Scope &operator=(const Scope &) = delete;

		  private:
			bool Recorded = false;
		};

		struct NamedScope {
			NamedScope(std::string_view fallback, const char *text, size_t length) {
				if (!IsEnabled()) {
					return;
				}

				if (length > 0 && text) {
					PushCopied({text, length});
				} else {
					Push(fallback);
				}
				Recorded = true;
			}
			~NamedScope() {
				if (Recorded) {
					Pop();
				}
			}

			NamedScope(const NamedScope &) = delete;
			NamedScope &operator=(const NamedScope &) = delete;

		  private:
			bool Recorded = false;
		};
	}
}
