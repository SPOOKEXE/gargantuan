#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gargantuan {
	// Where a frame's time went, as a tree of named zones.
	//
	// Samples accumulate for a second and are published as per-frame averages.
	// A chart redrawn every frame moves faster than the eye reads it, and
	// keeping every zone of every frame would cost more than it measures. The
	// single bad frame is what the F3 counter's minimum is for.
	class Profiler {
	  public:
		static constexpr size_t NONE = (size_t)-1;
		static constexpr double WINDOW_SECONDS = 1.0;
		static constexpr int MAXIMUM_DEPTH = 12;

		// Milliseconds per frame, averaged over the window that produced it
		struct Zone {
			std::string Name;
			size_t Parent = NONE;
			int Depth = 0;
			std::vector<size_t> Children;
			double Milliseconds = 0.0;
			double CallsPerFrame = 0.0;
		};

		// Counted rather than timed. Timing one draw measures how long it took
		// to write into a command buffer, which is not what a cylinder costs.
		struct Counter {
			std::string Name;
			double PerFrame = 0.0;
			uint64_t Total = 0;
		};

		struct Snapshot {
			std::vector<Zone> Zones;
			// In the order they were first opened, so rows keep a stable order
			std::vector<size_t> Roots;
			std::vector<Counter> Counters;
			uint64_t Frames = 0;
			double Seconds = 0.0;
			// Wall clock. Zones are measured against this rather than the sum
			// of the roots, which do not cover everything.
			double FrameMilliseconds = 0.0;

			bool Empty() const {
				return Zones.empty() && Counters.empty();
			}
		};

		// Takes effect at the next frame boundary; switching mid-frame would
		// leave the zone stack half measured
		void SetEnabled(bool enabled);
		bool IsEnabled() const;

		void BeginFrame(double now);
		void EndFrame(double now);

		void Begin(std::string_view name);
		void End();

		void Add(std::string_view name, uint64_t amount);

		// Adds time without opening a zone, for a loop running thousands of
		// times a frame where Begin and End would cost more than the work
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

	// Closes its zone however the scope is left, which several instrumented
	// paths rely on
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
