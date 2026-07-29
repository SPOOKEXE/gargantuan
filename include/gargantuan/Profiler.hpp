#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gargantuan {
	// Where a frame's time went, as a tree of named zones.
	//
	// Samples are accumulated for a second and then published as per-frame
	// averages, rather than kept per frame and drawn as they arrive. A chart
	// redrawn every frame is unreadable -- the bars move faster than the eye
	// resolves them -- and keeping every zone of every frame would cost more
	// than the thing being measured. What a second's worth of averages loses is
	// the single bad frame, which is what the F3 counter's minimum is for.
	//
	// The tree itself persists across the window. A zone is identified by its
	// name and its parent, so the same nesting each frame lands on the same
	// node and accumulating is a pointer walk rather than a rebuild.
	class Profiler {
	  public:
		static constexpr size_t NONE = (size_t)-1;
		// Long enough that the numbers settle, short enough to still be
		// following what the engine is doing now
		static constexpr double WINDOW_SECONDS = 1.0;
		// A runaway of nested zones is a bug in the instrumentation rather than
		// something to render, so it is bounded rather than trusted
		static constexpr int MAXIMUM_DEPTH = 12;

		// One zone, as published: everything in milliseconds per frame
		struct Zone {
			std::string Name;
			size_t Parent = NONE;
			int Depth = 0;
			std::vector<size_t> Children;
			double Milliseconds = 0.0;
			double CallsPerFrame = 0.0;
		};

		// Something counted rather than timed. Draw calls per primitive class
		// are the reason this exists: timing an individual draw measures how
		// long it took to write into a command buffer, which is not what
		// anyone means by the cost of a cylinder.
		struct Counter {
			std::string Name;
			double PerFrame = 0.0;
			uint64_t Total = 0;
		};

		struct Snapshot {
			std::vector<Zone> Zones;
			// Indices into Zones, in the order they were first opened, so the
			// chart's rows keep a stable order between snapshots
			std::vector<size_t> Roots;
			std::vector<Counter> Counters;
			uint64_t Frames = 0;
			double Seconds = 0.0;
			// What a whole frame took, wall clock. The zones are measured
			// against this rather than against the sum of the roots, because
			// the roots do not cover everything and pretending they do would
			// scale every bar by whatever was left out.
			double FrameMilliseconds = 0.0;

			bool Empty() const {
				return Zones.empty() && Counters.empty();
			}
		};

		// Takes effect at the next frame boundary. Switching mid-frame would
		// leave the zone stack half measured, and a tree with one arm missing
		// is worse than no tree.
		void SetEnabled(bool enabled);
		bool IsEnabled() const;

		void BeginFrame(double now);
		void EndFrame(double now);

		// Opens a zone under whichever one is currently open. Every Begin needs
		// its End, which is what ProfileScope is for.
		void Begin(std::string_view name);
		void End();

		void Add(std::string_view name, uint64_t amount);

		// Adds time to a child zone without opening and closing one, for a loop
		// that runs thousands of times a frame. Begin and End cost a name
		// lookup and two clock reads apiece, which at that count is more than
		// the work being measured; this lets the loop keep its own running
		// totals and hand them over once at the end.
		void AddZoneTime(std::string_view name, uint64_t nanoseconds, uint64_t calls);

		const Snapshot &Latest() const;
		// Whether anything has been published yet, so a reader can say it is
		// still gathering rather than drawing an empty chart
		bool HasSnapshot() const;

		// Reached from the renderer and from Luau callbacks without threading a
		// pointer through either, the same way the renderer itself is
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
		// Zones opened this frame are only measured when the frame started
		// enabled, so a toggle cannot produce a half-timed tree
		bool MeasuringFrame = false;

		double WindowStart = 0.0;
		uint64_t WindowFrames = 0;
		uint64_t FrameStartNanoseconds = 0;
		uint64_t WindowFrameNanoseconds = 0;
	};

	// Closes its zone however the scope is left, which matters because a good
	// few of the instrumented paths return early
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
