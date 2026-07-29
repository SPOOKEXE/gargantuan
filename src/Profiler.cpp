#include "gargantuan/Profiler.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace gargantuan {
	namespace {
		Profiler *CURRENT_PROFILER = nullptr;
	}

	Profiler *Profiler::GetCurrent() {
		return CURRENT_PROFILER;
	}

	void Profiler::SetCurrent(Profiler *profiler) {
		CURRENT_PROFILER = profiler;
	}

	void Profiler::SetEnabled(bool enabled) {
		PendingEnabled = enabled;
	}

	bool Profiler::IsEnabled() const {
		return Enabled;
	}

	bool Profiler::HasSnapshot() const {
		return Snapshotted;
	}

	const Profiler::Snapshot &Profiler::Latest() const {
		return Published;
	}

	size_t Profiler::FindOrCreate(size_t parent, std::string_view name) {
		const std::vector<size_t> &siblings = parent == NONE ? LiveRoots : Live[parent].Children;
		for (size_t index : siblings) {
			if (Live[index].Name == name) {
				return index;
			}
		}

		LiveZone zone;
		zone.Name = std::string(name);
		zone.Parent = parent;
		zone.Depth = parent == NONE ? 0 : Live[parent].Depth + 1;

		size_t index = Live.size();
		Live.push_back(std::move(zone));

		// Taken after the push, because pushing may have moved the vector out
		// from under a reference held across it
		if (parent == NONE) {
			LiveRoots.push_back(index);
		} else {
			Live[parent].Children.push_back(index);
		}
		return index;
	}

	void Profiler::Begin(std::string_view name) {
		if (!MeasuringFrame || Stack.size() >= (size_t)MAXIMUM_DEPTH) {
			// Still pushed, so the matching End has something to take off and
			// the tree does not reparent everything after it
			Stack.push_back({NONE, 0});
			return;
		}

		size_t parent = Stack.empty() ? NONE : Stack.back().Index;
		// A zone opened inside one that was skipped is skipped with it, or it
		// would attach to its grandparent and read as time that parent did not
		// spend there
		if (!Stack.empty() && parent == NONE) {
			Stack.push_back({NONE, 0});
			return;
		}

		Stack.push_back({FindOrCreate(parent, name), SDL_GetTicksNS()});
	}

	void Profiler::End() {
		if (Stack.empty()) {
			return;
		}

		Open open = Stack.back();
		Stack.pop_back();

		if (open.Index == NONE) {
			return;
		}

		Live[open.Index].Nanoseconds += SDL_GetTicksNS() - open.Start;
		Live[open.Index].Calls++;
	}

	void Profiler::Add(std::string_view name, uint64_t amount) {
		if (!MeasuringFrame) {
			return;
		}

		for (auto &counter : LiveCounters) {
			if (counter.Name == name) {
				counter.Total += amount;
				return;
			}
		}

		LiveCounters.push_back({std::string(name), amount});
	}

	void Profiler::BeginFrame(double now) {
		// The only place the switch is honoured, so a frame is measured whole
		// or not at all
		if (PendingEnabled != Enabled) {
			Enabled = PendingEnabled;

			// Whatever was gathered belongs to a window that is now over, and
			// the numbers in it were taken under different conditions
			Live.clear();
			LiveRoots.clear();
			LiveCounters.clear();
			WindowFrames = 0;
			WindowFrameNanoseconds = 0;
			WindowStart = now;

			if (!Enabled) {
				Published = {};
				Snapshotted = false;
			}
		}

		Stack.clear();
		MeasuringFrame = Enabled;
		FrameStartNanoseconds = SDL_GetTicksNS();

		if (Enabled && WindowStart == 0.0) {
			WindowStart = now;
		}
	}

	void Profiler::EndFrame(double now) {
		if (!MeasuringFrame) {
			return;
		}

		// Anything left open is instrumentation that returned without closing
		// its scope; closing it here keeps the next frame's tree honest
		while (!Stack.empty()) {
			End();
		}

		WindowFrames++;
		WindowFrameNanoseconds += SDL_GetTicksNS() - FrameStartNanoseconds;

		if (now - WindowStart >= WINDOW_SECONDS) {
			Publish(now);
		}
	}

	void Profiler::Publish(double now) {
		double frames = (double)std::max<uint64_t>(WindowFrames, 1);

		Snapshot snapshot;
		snapshot.Frames = WindowFrames;
		snapshot.Seconds = now - WindowStart;
		snapshot.FrameMilliseconds = (double)WindowFrameNanoseconds / frames / 1000000.0;

		snapshot.Zones.reserve(Live.size());
		for (const auto &live : Live) {
			Zone zone;
			zone.Name = live.Name;
			zone.Parent = live.Parent;
			zone.Depth = live.Depth;
			zone.Children = live.Children;
			zone.Milliseconds = (double)live.Nanoseconds / frames / 1000000.0;
			zone.CallsPerFrame = (double)live.Calls / frames;
			snapshot.Zones.push_back(std::move(zone));
		}
		snapshot.Roots = LiveRoots;

		snapshot.Counters.reserve(LiveCounters.size());
		for (const auto &live : LiveCounters) {
			snapshot.Counters.push_back({live.Name, (double)live.Total / frames, live.Total});
		}
		// Alphabetical, so a class appearing or disappearing does not shuffle
		// the rest of the list under the reader's eye
		std::sort(snapshot.Counters.begin(), snapshot.Counters.end(), [](const Counter &a, const Counter &b) {
			return a.Name < b.Name;
		});

		Published = std::move(snapshot);
		Snapshotted = true;

		// The tree is kept and only its numbers are cleared: the shape of a
		// frame is the same from one second to the next, and rebuilding it
		// would throw away the row order the chart depends on
		for (auto &live : Live) {
			live.Nanoseconds = 0;
			live.Calls = 0;
		}
		for (auto &counter : LiveCounters) {
			counter.Total = 0;
		}

		WindowFrames = 0;
		WindowFrameNanoseconds = 0;
		WindowStart = now;
	}

	void ProfilerCount(std::string_view name, uint64_t amount) {
		if (Profiler *profiler = Profiler::GetCurrent()) {
			profiler->Add(name, amount);
		}
	}
} // namespace gargantuan
