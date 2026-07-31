#include "gargantuan/FrameGraph.hpp"

#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <utility>

namespace gargantuan::FrameGraph {
	namespace {
		bool Enabled = false;
		// Never publish a frame whose recording began after the frame started.
		bool FramePartial = true;

		// Double-buffered so readers only observe complete frames.
		std::vector<Sample> Recording;
		std::vector<Sample> Completed;

		std::vector<Counter> RecordingCounters;
		std::vector<Counter> CompletedCounters;

		// deque keeps Sample string_views valid; entries are reused across frames.
		std::deque<std::string> RecordingNames;
		std::deque<std::string> CompletedNames;
		size_t RecordingNameCount = 0;

		// Only recorded depths have stack entries; deeper zones still affect balance.
		std::vector<size_t> OpenStack;
		uint32_t Depth = 0;

		uint64_t FrameStartNanoseconds = 0;
		size_t Dropped = 0;
		size_t CompletedDropped = 0;
		float CompletedMilliseconds = 0.0f;

		float SinceFrameStart(uint64_t now) {
			return (float)((double)(now - FrameStartNanoseconds) / 1000000.0);
		}

		// History owns zone names; frame-local string_views do not outlive their pool.
		std::vector<std::string> HistoryNames;
		std::map<std::string, uint32_t, std::less<>> HistoryNameIds;
		constexpr uint32_t NoName = UINT32_MAX;

		// Seen is independent of duration because a recorded zone may measure 0 ms.
		std::vector<float> FrameMaximums;
		std::vector<uint8_t> FrameSeen;
		std::vector<uint32_t> FrameTouched;

		std::vector<std::vector<float>> RecentRings;
		size_t RecentCursor = 0;

		// Frame totals over the same window as RecentRings, kept apart from
		// History because jitter is read every frame and History holds twenty
		// thousand of them.
		float RecentFrameRing[RecentFrames] = {};
		size_t RecentFrameCount = 0;

		struct HistoryFrame {
			double Time = 0.0;
			float Milliseconds = 0.0f;
			std::vector<std::pair<uint32_t, float>> Zones;
		};

		std::vector<HistoryFrame> History;
		size_t HistoryStart = 0;
		size_t HistoryCount = 0;
		// Report overflow so snapshots never appear complete when names were dropped.
		size_t HistoryNamesDropped = 0;

		void ClearHistory() {
			for (auto &ring : RecentRings) {
				std::fill(ring.begin(), ring.end(), 0.0f);
			}
			RecentCursor = 0;
			std::fill(std::begin(RecentFrameRing), std::end(RecentFrameRing), 0.0f);
			RecentFrameCount = 0;
			HistoryStart = 0;
			HistoryCount = 0;
			HistoryNamesDropped = 0;
		}

		uint32_t HistoryNameId(std::string_view name) {
			auto found = HistoryNameIds.find(name);
			if (found != HistoryNameIds.end()) {
				return found->second;
			}
			if (HistoryNames.size() >= MaxHistoryNames) {
				HistoryNamesDropped++;
				return NoName;
			}

			uint32_t id = (uint32_t)HistoryNames.size();
			HistoryNames.emplace_back(name);
			HistoryNameIds.emplace(HistoryNames.back(), id);
			RecentRings.emplace_back(RecentFrames, 0.0f);
			FrameMaximums.push_back(0.0f);
			FrameSeen.push_back(0);
			return id;
		}

		void RecordHistory(const std::vector<Sample> &samples, float frameMilliseconds, uint64_t nowNanoseconds) {
			for (const Sample &sample : samples) {
				uint32_t id = HistoryNameId(sample.Name);
				if (id == NoName) {
					continue;
				}
				if (!FrameSeen[id]) {
					FrameSeen[id] = 1;
					FrameTouched.push_back(id);
				}
				FrameMaximums[id] = std::max(FrameMaximums[id], sample.Milliseconds);
			}

			// Write zero for absent zones so stale readings cannot survive in the ring.
			for (size_t id = 0; id < RecentRings.size(); id++) {
				RecentRings[id][RecentCursor] = FrameMaximums[id];
			}
			RecentFrameRing[RecentCursor] = frameMilliseconds;
			RecentFrameCount = std::min(RecentFrameCount + 1, RecentFrames);
			RecentCursor = (RecentCursor + 1) % RecentFrames;

			if (History.size() < MaxHistoryFrames) {
				History.resize(MaxHistoryFrames);
			}
			size_t slot = (HistoryStart + HistoryCount) % MaxHistoryFrames;
			if (HistoryCount == MaxHistoryFrames) {
				HistoryStart = (HistoryStart + 1) % MaxHistoryFrames;
			} else {
				HistoryCount++;
			}

			HistoryFrame &frame = History[slot];
			frame.Time = (double)nowNanoseconds / 1000000000.0;
			frame.Milliseconds = frameMilliseconds;
			frame.Zones.clear();
			for (uint32_t id : FrameTouched) {
				frame.Zones.emplace_back(id, FrameMaximums[id]);
			}

			while (HistoryCount > 1 && frame.Time - History[HistoryStart].Time > HistorySeconds) {
				HistoryStart = (HistoryStart + 1) % MaxHistoryFrames;
				HistoryCount--;
			}

			for (uint32_t id : FrameTouched) {
				FrameMaximums[id] = 0.0f;
				FrameSeen[id] = 0;
			}
			FrameTouched.clear();
		}

		// The deque node keeps the returned view valid for the frame.
		std::string_view Intern(std::string_view name) {
			if (RecordingNameCount == RecordingNames.size()) {
				RecordingNames.emplace_back(name);
			} else {
				RecordingNames[RecordingNameCount].assign(name);
			}
			return RecordingNames[RecordingNameCount++];
		}
	}

	void SetEnabled(bool enabled) {
		if (enabled == Enabled) {
			return;
		}

		Enabled = enabled;
		Recording.clear();
		RecordingCounters.clear();
		OpenStack.clear();
		Depth = 0;
		RecordingNameCount = 0;
		FramePartial = true;
		// Clear history across recording gaps; prior samples may describe another scene.
		ClearHistory();
	}

	bool IsEnabled() {
		return Enabled;
	}

	void BeginFrame() {
		if (!Enabled) {
			return;
		}
		Recording.clear();
		OpenStack.clear();
		Depth = 0;
		Dropped = 0;
		RecordingNameCount = 0;
		RecordingCounters.clear();
		FramePartial = false;
		FrameStartNanoseconds = SDL_GetTicksNS();
	}

	void EndFrame() {
		if (!Enabled) {
			return;
		}

		// Close leaked zones at frame end rather than dropping measured work.
		uint64_t now = SDL_GetTicksNS();
		while (!OpenStack.empty()) {
			Sample &sample = Recording[OpenStack.back()];
			sample.Milliseconds = SinceFrameStart(now) - sample.StartMilliseconds;
			OpenStack.pop_back();
		}
		Depth = 0;

		// Never replace the last complete frame with a partial frame.
		if (FramePartial) {
			Recording.clear();
			RecordingCounters.clear();
			RecordingNameCount = 0;
			return;
		}

		CompletedMilliseconds = SinceFrameStart(now);
		CompletedDropped = Dropped;
		RecordHistory(Recording, CompletedMilliseconds, now);
		Completed.swap(Recording);
		CompletedCounters.swap(RecordingCounters);
		// Swap name storage with the samples whose views reference it.
		CompletedNames.swap(RecordingNames);
		RecordingNameCount = 0;
	}

	void RecordCounter(const char *name, double value, bool isTime) {
		if (!Enabled || !name) {
			return;
		}

		// Linear, because a frame has tens of counters and not thousands, and
		// because keeping them in the order the frame counted them is what puts
		// a renderer's counters next to each other without a sort.
		for (Counter &counter : RecordingCounters) {
			// By address first: the contract is that the name outlives the
			// program, so the same call site is the same pointer. The compare
			// is for the two call sites that pass the same literal and did not
			// get merged into one.
			if (counter.Name == name || std::strcmp(counter.Name, name) == 0) {
				counter.Value += value;
				counter.Samples++;
				return;
			}
		}

		RecordingCounters.push_back({.Name = name, .Value = value, .Samples = 1, .IsTime = isTime});
	}

	const std::vector<Counter> &GetCounters() {
		return CompletedCounters;
	}

	void Push(std::string_view name) {
		// Track skipped depth so Pop remains balanced.
		if (Depth >= MaxDepth) {
			Depth++;
			return;
		}

		if (Recording.size() >= MaxSamples) {
			Dropped++;
			Depth++;
			return;
		}

		// Only recorded zones are on the stack, and a zone is only skipped when
		// its parent was, so the top of the stack is this zone's parent.
		uint32_t parent = OpenStack.empty() ? NoParent : (uint32_t)OpenStack.back();

		OpenStack.push_back(Recording.size());
		Recording.push_back({
			.Name = name,
			.Depth = Depth,
			.Parent = parent,
			.StartMilliseconds = SinceFrameStart(SDL_GetTicksNS()),
			.Milliseconds = 0.0f,
		});
		Depth++;
	}

	void PushCopied(std::string_view name) {
		// Interned before the depth checks would drop it, because a name is only
		// worth copying when the sample that keeps it is going to exist.
		if (Depth >= MaxDepth || Recording.size() >= MaxSamples) {
			Push(name);
			return;
		}
		Push(Intern(name));
	}

	void Pop() {
		if (Depth == 0) {
			return;
		}
		Depth--;

		// Only zones that were actually recorded have an entry to close.
		if (Depth >= MaxDepth || OpenStack.empty()) {
			return;
		}

		Sample &sample = Recording[OpenStack.back()];
		sample.Milliseconds = SinceFrameStart(SDL_GetTicksNS()) - sample.StartMilliseconds;
		OpenStack.pop_back();
	}

	const std::vector<Sample> &GetSamples() {
		return Completed;
	}

	size_t GetDropped() {
		return CompletedDropped;
	}

	float GetFrameMilliseconds() {
		return CompletedMilliseconds;
	}

	// Mean absolute change from one frame to the next. A run alternating 4 ms
	// and 12 ms averages 8 like a steady 8 does, and looks nothing like it.
	float GetFrameJitter() {
		if (RecentFrameCount < 2) {
			return 0.0f;
		}

		size_t start = (RecentCursor + RecentFrames - RecentFrameCount) % RecentFrames;
		double total = 0.0;
		float previous = RecentFrameRing[start];
		for (size_t offset = 1; offset < RecentFrameCount; offset++) {
			float value = RecentFrameRing[(start + offset) % RecentFrames];
			total += std::abs(value - previous);
			previous = value;
		}
		return (float)(total / (double)(RecentFrameCount - 1));
	}

	float GetRecentMax(std::string_view name) {
		auto found = HistoryNameIds.find(name);
		if (found == HistoryNameIds.end()) {
			return 0.0f;
		}

		const std::vector<float> &ring = RecentRings[found->second];
		float worst = 0.0f;
		for (float reading : ring) {
			worst = std::max(worst, reading);
		}
		return worst;
	}

	size_t GetHistoryFrames() {
		return HistoryCount;
	}

	double GetHistorySeconds() {
		if (HistoryCount < 2) {
			return 0.0;
		}
		size_t last = (HistoryStart + HistoryCount - 1) % MaxHistoryFrames;
		return History[last].Time - History[HistoryStart].Time;
	}

	namespace {
		// Nearest-rank, on a copy the caller already owns. Not interpolated: with
		// thousands of readings the neighbouring ones are indistinguishable, and an
		// interpolated p99 can report a number no frame actually took.
		float Percentile(std::vector<float> &sorted, double fraction) {
			if (sorted.empty()) {
				return 0.0f;
			}
			std::sort(sorted.begin(), sorted.end());
			size_t rank = (size_t)(fraction * (double)(sorted.size() - 1) + 0.5);
			return sorted[std::min(rank, sorted.size() - 1)];
		}

		// How many of the worst frames the snapshot lists individually. The summary
		// says a zone spikes; these say which frames, so two zones spiking together
		// can be told from two spiking apart.
		constexpr size_t WORST_FRAMES = 40;
		// Zones listed per worst frame, largest first.
		constexpr size_t WORST_FRAME_ZONES = 6;
	}

	bool WriteSnapshot(const char *path) {
		if (!path || HistoryCount == 0) {
			return false;
		}

		std::ofstream out(path, std::ios::trunc);
		if (!out) {
			return false;
		}

		// Gathered by name id, in one pass over the window.
		std::vector<std::vector<float>> readings(HistoryNames.size());
		std::vector<float> frameMilliseconds;
		frameMilliseconds.reserve(HistoryCount);

		double firstTime = History[HistoryStart].Time;
		for (size_t offset = 0; offset < HistoryCount; offset++) {
			const HistoryFrame &frame = History[(HistoryStart + offset) % MaxHistoryFrames];
			frameMilliseconds.push_back(frame.Milliseconds);
			for (const auto &[id, milliseconds] : frame.Zones) {
				readings[id].push_back(milliseconds);
			}
		}

		double span = GetHistorySeconds();
		std::vector<float> frameSorted = frameMilliseconds;
		double frameTotal = 0.0;
		float frameWorst = 0.0f;
		for (float milliseconds : frameMilliseconds) {
			frameTotal += milliseconds;
			frameWorst = std::max(frameWorst, milliseconds);
		}

		char line[512];
		out << "gargantuan profiler snapshot\n";
		std::snprintf(
			line,
			sizeof(line),
			"window   %zu frames over %.3f s (bounds: %.1f s, %zu frames)\n",
			HistoryCount,
			span,
			HistorySeconds,
			MaxHistoryFrames
		);
		out << line;
		std::snprintf(
			line,
			sizeof(line),
			"frame ms mean %.3f  max %.3f  p99 %.3f  p50 %.3f  jitter %.3f\n",
			frameTotal / (double)HistoryCount,
			frameWorst,
			Percentile(frameSorted, 0.99),
			Percentile(frameSorted, 0.50),
			GetFrameJitter()
		);
		out << line;
		if (HistoryNamesDropped > 0) {
			std::snprintf(
				line, sizeof(line), "WARNING  %zu zone names past the %zu tracked were not recorded\n",
				HistoryNamesDropped, MaxHistoryNames
			);
			out << line;
		}

		// Every number below is that zone's worst single reading in a frame, the
		// same quantity the panel's RMAX column shows. Not a per-frame total: a
		// zone that opens once a camera would otherwise read as the sum of six
		// cameras and not compare with anything else here.
		out << "\nper zone, worst reading in a frame. sorted by max.\n";
		std::snprintf(
			line, sizeof(line), "%-34s %7s %8s %8s %8s %8s\n", "zone", "frames", "mean", "p50", "p99", "max"
		);
		out << line;

		std::vector<uint32_t> order;
		for (uint32_t id = 0; id < (uint32_t)readings.size(); id++) {
			if (!readings[id].empty()) {
				order.push_back(id);
			}
		}
		std::sort(order.begin(), order.end(), [&](uint32_t left, uint32_t right) {
			float leftMax = *std::max_element(readings[left].begin(), readings[left].end());
			float rightMax = *std::max_element(readings[right].begin(), readings[right].end());
			return leftMax > rightMax;
		});

		for (uint32_t id : order) {
			std::vector<float> &zone = readings[id];
			double total = 0.0;
			float worst = 0.0f;
			for (float milliseconds : zone) {
				total += milliseconds;
				worst = std::max(worst, milliseconds);
			}
			std::vector<float> sorted = zone;
			std::snprintf(
				line,
				sizeof(line),
				"%-34s %7zu %8.3f %8.3f %8.3f %8.3f\n",
				HistoryNames[id].c_str(),
				zone.size(),
				total / (double)zone.size(),
				Percentile(sorted, 0.50),
				Percentile(sorted, 0.99),
				worst
			);
			out << line;
		}

		// The worst frames themselves. A summary says one zone spikes; this says
		// whether it spikes alone or whether the whole frame went with it.
		std::vector<size_t> worstOrder(HistoryCount);
		for (size_t offset = 0; offset < HistoryCount; offset++) {
			worstOrder[offset] = offset;
		}
		std::sort(worstOrder.begin(), worstOrder.end(), [&](size_t left, size_t right) {
			return frameMilliseconds[left] > frameMilliseconds[right];
		});

		std::snprintf(line, sizeof(line), "\nworst %zu frames, and the biggest zones in each\n",
			std::min(WORST_FRAMES, HistoryCount));
		out << line;

		size_t listed = std::min(WORST_FRAMES, HistoryCount);
		for (size_t rank = 0; rank < listed; rank++) {
			size_t offset = worstOrder[rank];
			const HistoryFrame &frame = History[(HistoryStart + offset) % MaxHistoryFrames];

			std::snprintf(
				line, sizeof(line), "  +%8.4f s  %8.3f ms ", frame.Time - firstTime, frame.Milliseconds
			);
			out << line;

			std::vector<std::pair<uint32_t, float>> zones = frame.Zones;
			std::sort(zones.begin(), zones.end(), [](const auto &left, const auto &right) {
				return left.second > right.second;
			});
			for (size_t index = 0; index < zones.size() && index < WORST_FRAME_ZONES; index++) {
				std::snprintf(
					line, sizeof(line), " %s %.3f", HistoryNames[zones[index].first].c_str(), zones[index].second
				);
				out << line;
			}
			out << "\n";
		}

		return out.good();
	}
}
