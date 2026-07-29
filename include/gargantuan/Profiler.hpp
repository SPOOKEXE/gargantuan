#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gargantuan {
	// Named zone tree, averaged per frame over a one-second window.
	// The F3 minimum captures isolated bad frames without retaining every frame.
	class Profiler {
	  public:
		static constexpr size_t NONE = (size_t)-1;
		static constexpr double WINDOW_SECONDS = 1.0;
		static constexpr int MAXIMUM_DEPTH = 12;

		struct Zone {
			std::string Name;
			size_t Parent = NONE;
			int Depth = 0;
			std::vector<size_t> Children;
			// Per-frame window average.
			double Milliseconds = 0.0;
			double CallsPerFrame = 0.0;
		};

		// Count draws; command-buffer write time is not renderer cost.
		struct Counter {
			std::string Name;
			double PerFrame = 0.0;
			uint64_t Total = 0;
		};

		struct Snapshot {
			std::vector<Zone> Zones;
			// First-open order keeps rows stable.
			std::vector<size_t> Roots;
			std::vector<Counter> Counters;
			uint64_t Frames = 0;
			double Seconds = 0.0;
			// Wall-clock frame time; roots do not cover the whole frame.
			double FrameMilliseconds = 0.0;

			bool Empty() const {
				return Zones.empty() && Counters.empty();
			}
		};

		// Applies at the next frame boundary to preserve the zone stack.
		void SetEnabled(bool enabled);
		bool IsEnabled() const;

		void BeginFrame(double now);
		void EndFrame(double now);

		void Begin(std::string_view name);
		void End();

		void Add(std::string_view name, uint64_t amount);

		// Adds nanoseconds and calls without per-iteration scope overhead.
		void AddZoneTime(std::string_view name, uint64_t nanoseconds, uint64_t calls);

		const Snapshot &Latest() const;
		bool HasSnapshot() const;

		static Profiler *GetCurrent();
		static void SetCurrent(Profiler *profiler);

	  private:
		struct LiveZone {
			std::string Name;
			size_t Parent = NONE;
			int Depth = 0;
			std::vector<size_t> Children;
			uint64_t Nanoseconds = 0;
			uint64_t Calls = 0;
		};

		struct LiveCounter {
			std::string Name;
			uint64_t Total = 0;
		};

		struct Open {
			size_t Index = NONE;
			uint64_t Start = 0;
		};

		size_t FindOrCreate(size_t parent, std::string_view name);
		void Publish(double now);

		std::vector<LiveZone> Live;
		std::vector<size_t> LiveRoots;
		std::vector<LiveCounter> LiveCounters;
		std::vector<Open> Stack;

		Snapshot Published;
		bool Snapshotted = false;

		bool Enabled = false;
		bool PendingEnabled = false;
		bool MeasuringFrame = false;

		double WindowStart = 0.0;
		uint64_t WindowFrames = 0;
		uint64_t FrameStartNanoseconds = 0;
		uint64_t WindowFrameNanoseconds = 0;
	};

	// Closes its zone on every scope exit path.
	struct ProfileScope {
		explicit ProfileScope(std::string_view name) : Owner(Profiler::GetCurrent()) {
			if (Owner) {
				Owner->Begin(name);
			}
		}

		~ProfileScope() {
			if (Owner) {
				Owner->End();
			}
		}

		ProfileScope(const ProfileScope &) = delete;
		ProfileScope &operator=(const ProfileScope &) = delete;

	  private:
		Profiler *Owner = nullptr;
	};

	void ProfilerCount(std::string_view name, uint64_t amount);

#define G_PROFILE_JOIN2(a, b) a##b
#define G_PROFILE_JOIN(a, b) G_PROFILE_JOIN2(a, b)
#define G_PROFILE(name) ::gargantuan::ProfileScope G_PROFILE_JOIN(profileScope, __LINE__)(name)
} // namespace gargantuan
