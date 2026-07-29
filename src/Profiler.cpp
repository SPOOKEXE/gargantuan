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

		// Reacquire by index after vector relocation.
		if (parent == NONE) {
			LiveRoots.push_back(index);
		} else {
			Live[parent].Children.push_back(index);
		}
		return index;
	}

	void Profiler::Begin(std::string_view name) {
		if (!MeasuringFrame || Stack.size() >= (size_t)MAXIMUM_DEPTH) {
			// Keep stack balance without reparenting later zones.
			Stack.push_back({NONE, 0});
			return;
		}

		size_t parent = Stack.empty() ? NONE : Stack.back().Index;
		// Skip descendants of depth-limited zones.
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

	void Profiler::AddZoneTime(std::string_view name, uint64_t nanoseconds, uint64_t calls) {
		if (!MeasuringFrame) {
			return;
		}

		size_t parent = Stack.empty() ? NONE : Stack.back().Index;
		// Skip timing inside a depth-limited zone.
		if (!Stack.empty() && parent == NONE) {
			return;
		}

		size_t index = FindOrCreate(parent, name);
		Live[index].Nanoseconds += nanoseconds;
		Live[index].Calls += calls;
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
		// Toggle only at frame boundaries.
		if (PendingEnabled != Enabled) {
			Enabled = PendingEnabled;

			// Discard the prior enable-state window.
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

		// Close leaked scopes at frame end.
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
		// Alphabetical order prevents row churn.
		std::sort(snapshot.Counters.begin(), snapshot.Counters.end(), [](const Counter &a, const Counter &b) {
			return a.Name < b.Name;
		});

		Published = std::move(snapshot);
		Snapshotted = true;

		// Preserve tree order; clear only window totals.
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
