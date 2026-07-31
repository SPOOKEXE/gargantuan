#include "gargantuan/Engine.hpp"
#include "gargantuan/FrameGraph.hpp"
#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Part.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/DataModel.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/render/MeshProvider.hpp"
#include "gargantuan/render/RenderProvider.hpp"
#include "gargantuan/scripting/ScriptEngine.hpp"
#include "gargantuan/services/Workspace.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fwd.hpp>
#include <glm/glm.hpp>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <memory>
#include <vector>
#include <stdexcept>

namespace gargantuan {
	Engine::Engine() {
		this->Gpu = SDL_CreateGPUDevice(
			SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_MSL, true, nullptr
		);
		if (!Gpu) {
			throw std::runtime_error("Failed to instantiate GPU");
		}

		this->Window = SDL_CreateWindow(
			"Gargantuan",
			ViewportSize.x,
			ViewportSize.y,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY
		);
		if (!Window) {
			throw std::runtime_error("Failed to instantiate window");
		}

		this->RenderProvider = new class RenderProvider(Window, Gpu);
		// Camera:Render() reaches the renderer through this
		RenderProvider::SetCurrent(this->RenderProvider);

		this->ScriptEngine = new class ScriptEngine();

		DataModel = std::make_shared<gargantuan::DataModel>();
		DataModel->Name = "Welcome To Hell";

		auto workspace = this->DataModel->GetService("Workspace");
		this->Workspace = std::dynamic_pointer_cast<gargantuan::Workspace>(workspace);

		auto lighting = this->DataModel->GetService("Lighting");
		this->Lighting = std::dynamic_pointer_cast<gargantuan::Lighting>(lighting);

		auto runService = this->DataModel->GetService("RunService");
		this->RunService = std::dynamic_pointer_cast<gargantuan::RunService>(runService);

		auto uis = this->DataModel->GetService("UserInputService");
		this->UserInputService = std::dynamic_pointer_cast<gargantuan::UserInputService>(uis);

		auto tweenService = this->DataModel->GetService("TweenService");
		this->TweenService = std::dynamic_pointer_cast<gargantuan::TweenService>(tweenService);

		auto renderSettings = this->DataModel->GetService("RenderSettings");
		this->RenderSettings = std::dynamic_pointer_cast<gargantuan::RenderSettings>(renderSettings);

		StackValue<Instance::Pointer>::Push(ScriptEngine->L, this->DataModel);
		lua_pushvalue(ScriptEngine->L, -1);
		lua_setglobal(ScriptEngine->L, "game");

		RegisterSystems();
	}

	Engine::~Engine() {
		SDL_Log("destroying window");
		SDL_ReleaseWindowFromGPUDevice(Gpu, Window);
		SDL_DestroyWindow(Window);

		SDL_Log("destroying mesh provider");
		MeshProvider::DestroyAllGpuMeshes(Gpu);

		RenderProvider->Destroy();

		SDL_Log("destroying gpu %s", Gpu ? "exists" : "not exist");
		SDL_DestroyGPUDevice(Gpu);
		Gpu = nullptr;
		SDL_Log("done destroying gpu");
	}

	void Engine::ProcessEvent(SDL_Event event) {
		switch (event.type) {
		case SDL_EVENT_QUIT:
			SDL_Log("Stopping");
			IsRunning = false;
			return;
		}
	}

	// Where the frame went, averaged over the run. Tracy gives far more, but
	// this needs nothing attached and survives being run from a script.
	void Engine::ReportSystemTotals(double elapsed) const {
		if (ProfiledFrames == 0) return;

		double frames = (double)ProfiledFrames;
		SDL_Log("--- %llu frames in %.2f s (%.1f fps, %.2f ms a frame) ---",
			(unsigned long long)ProfiledFrames, elapsed, frames / elapsed, (elapsed * 1000.0) / frames);

		size_t systemIndex = 0;
		double total = 0.0;
		for (size_t index = 0; index < (size_t)ecs::Phase::Count; index++) {
			auto phase = (ecs::Phase)index;
			for (const auto &system : Scheduler.GetSystems(phase)) {
				if (systemIndex >= SystemTotals.size()) break;
				double average = SystemTotals[systemIndex++] / frames;
				total += average;
				SDL_Log("  %-10s %-20s %7.3f ms", GetPhaseName(phase).data(), system.Name.data(), average);
			}
		}
		SDL_Log("  %-10s %-20s %7.3f ms", "", "systems total", total);
	}

	void Engine::ProfileAndExit(double seconds) {
		AutomaticProfileSeconds = seconds;
		AutomaticProfileStarted = 0.0;
		// The per-zone history only accumulates while the frame graph is on,
		// which is otherwise something only F5 turns on. Without it an
		// unattended run reports system totals and nothing underneath them.
		ShowSystemTimings = true;
	}

	void Engine::UpdateAutomaticProfile(double now) {
		if (AutomaticProfileSeconds < 0.0) {
			return;
		}

		// The clock starts at the first frame, not at construction, so the
		// window is time actually spent rendering
		if (AutomaticProfileStarted == 0.0) {
			AutomaticProfileStarted = now;
			return;
		}

		// Totalled every frame of the run, so the report is an average over the
		// whole window rather than whatever the last frame happened to cost.
		{
			size_t systemIndex = 0;
			for (size_t index = 0; index < (size_t)ecs::Phase::Count; index++) {
				for (const auto &system : Scheduler.GetSystems((ecs::Phase)index)) {
					if (SystemTotals.size() <= systemIndex) SystemTotals.push_back(0.0);
					SystemTotals[systemIndex++] += system.LastMilliseconds;
				}
			}
			ProfiledFrames++;
		}

		if (now - AutomaticProfileStarted >= AutomaticProfileSeconds) {
			double elapsed = now - AutomaticProfileStarted;
			SDL_Log("Ran for %.1f s, stopping", elapsed);
			ReportSystemTotals(elapsed);
			// The zones under those systems, which is the half of the answer the
			// totals cannot show.
			WriteProfilerSnapshot();
			AutomaticProfileSeconds = -1.0;
			IsRunning = false;
		}
	}

	namespace {
		// What part of the engine a zone belongs to, or Count for "whatever ran
		// it". The schedule is where this is really decided -- it already names
		// the parts -- so a system decides for everything it opens, and the
		// prefixes below only catch zones the schedule does not reach.
		ProfilerCategory ClassifyZone(std::string_view name) {
			struct Rule {
				std::string_view Name;
				ProfilerCategory Category;
			};

			static constexpr Rule SYSTEMS[] = {
				{"render.draw", ProfilerCategory::Render},
				{"gpu.wait", ProfilerCategory::Render},
				{"lighting.syncRows", ProfilerCategory::Render},
				{"physics.step", ProfilerCategory::Physics},
				// Gameplay: the signals scripts connect to, and the tweens and
				// camera step that run beside them.
				{"simulation.step", ProfilerCategory::Luau},
				{"script.step", ProfilerCategory::Luau},
			};

			for (const auto &rule : SYSTEMS) {
				if (name == rule.Name) {
					return rule.Category;
				}
			}

			if (name.starts_with("physics.")) {
				return ProfilerCategory::Physics;
			}
			if (name.starts_with("luau.") || name.starts_with("signal.")) {
				return ProfilerCategory::Luau;
			}
			return ProfilerCategory::Count;
		}

		ProfilerCategory TabCategory(ProfilerTab tab) {
			switch (tab) {
			case ProfilerTab::Render: return ProfilerCategory::Render;
			case ProfilerTab::Physics: return ProfilerCategory::Physics;
			case ProfilerTab::Luau: return ProfilerCategory::Luau;
			default: return ProfilerCategory::Engine;
			}
		}
	} // namespace

	// The last completed frame, filtered to the open tab. Five passes over the
	// samples: what each zone belongs to, what each part of the engine spent,
	// what this tab shows, which of those rows stop short, and the rows.
	void Engine::BuildProfilerView() {
		const auto &samples = FrameGraph::GetSamples();
		size_t count = samples.size();

		ProfilerZoneCategories.assign(count, ProfilerCategory::Engine);
		ProfilerZoneRoots.assign(count, 0);
		ProfilerZoneHasChildren.assign(count, 0);
		ProfilerZoneChildTotals.assign(count, 0.0f);
		ProfilerZoneVisible.assign(count, 0);

		Profiler.FrameMilliseconds = FrameGraph::GetFrameMilliseconds();
		Profiler.FrameJitter = FrameGraph::GetFrameJitter();
		Profiler.Dropped = FrameGraph::GetDropped();
		for (float &total : Profiler.CategoryMilliseconds) {
			total = 0.0f;
		}
		Profiler.Rows.clear();

		// A child always follows its parent, so one pass forwards settles both
		// the category a zone inherits and the depth its tab measures from.
		for (size_t index = 0; index < count; index++) {
			const FrameGraph::Sample &sample = samples[index];
			bool hasParent = sample.Parent != FrameGraph::NoParent && sample.Parent < index;

			ProfilerCategory inherited =
				hasParent ? ProfilerZoneCategories[sample.Parent] : ProfilerCategory::Engine;
			ProfilerCategory own = ClassifyZone(sample.Name);
			ProfilerCategory category = own == ProfilerCategory::Count ? inherited : own;

			ProfilerZoneCategories[index] = category;
			// Where its part of the engine started, so a tab showing a subtree
			// starts at the left margin instead of six levels in.
			ProfilerZoneRoots[index] =
				hasParent && category == inherited ? ProfilerZoneRoots[sample.Parent] : sample.Depth;

			if (hasParent) {
				ProfilerZoneHasChildren[sample.Parent] = 1;
				ProfilerZoneChildTotals[sample.Parent] += sample.Milliseconds;
			}
		}

		// Exclusive time: what a zone spent itself, not what it spent plus
		// everything it called. Inclusive totals double count the moment one
		// part of the engine runs inside another -- a script that renders a
		// camera -- and four totals adding up to more than the frame are worse
		// than no totals at all.
		float accounted = 0.0f;
		for (size_t index = 0; index < count; index++) {
			float self = std::max(samples[index].Milliseconds - ProfilerZoneChildTotals[index], 0.0f);
			Profiler.CategoryMilliseconds[(size_t)ProfilerZoneCategories[index]] += self;
			accounted += self;
		}
		// Whatever the frame spent outside any zone at all is nobody's but the
		// engine's, and leaving it out would make the summary lie by omission.
		Profiler.CategoryMilliseconds[(size_t)ProfilerCategory::Engine] +=
			std::max(Profiler.FrameMilliseconds - accounted, 0.0f);

		ProfilerCategory wanted = TabCategory(Profiler.Tab);
		for (size_t index = 0; index < count; index++) {
			// Depth measured from the tab's own root, which is what the rows are
			// indented by. Only meaningful on the category tabs; Main and Full set
			// their own rule below.
			uint32_t relative = samples[index].Depth - ProfilerZoneRoots[index];
			switch (Profiler.Tab) {
			// The schedule and nothing under it: the frame, its phases, its
			// systems. Enough to say which part of the frame to open next.
			case ProfilerTab::Main: ProfilerZoneVisible[index] = samples[index].Depth <= 2 ? 1 : 0; break;
			case ProfilerTab::Full: ProfilerZoneVisible[index] = 1; break;
			// Not a slice of the frame's time at all -- its rows come from
			// BuildCounterRows, and the zones have nothing to say on it.
			case ProfilerTab::Counters: ProfilerZoneVisible[index] = 0; break;
			default:
				ProfilerZoneVisible[index] =
					ProfilerZoneCategories[index] == wanted && relative <= Profiler.DepthLimit ? 1 : 0;
				break;
			}
		}

		// A row whose children this tab hides is marked rather than left looking
		// like a leaf: "physics.step 8 ms" with nothing under it means one thing
		// on Main and something else entirely on Physics.
		for (size_t index = 0; index < count; index++) {
			const FrameGraph::Sample &sample = samples[index];
			if (!ProfilerZoneVisible[index] || sample.Parent == FrameGraph::NoParent ||
				sample.Parent >= index) {
				continue;
			}
			ProfilerZoneHasChildren[sample.Parent] = 2;
		}

		for (size_t index = 0; index < count; index++) {
			if (!ProfilerZoneVisible[index]) {
				continue;
			}

			const FrameGraph::Sample &sample = samples[index];
			uint32_t depth = sample.Depth;
			if (Profiler.Tab != ProfilerTab::Main && Profiler.Tab != ProfilerTab::Full) {
				depth -= ProfilerZoneRoots[index];
			}

			Profiler.Rows.push_back({
				.Name = sample.Name,
				.Depth = depth,
				.StartMilliseconds = sample.StartMilliseconds,
				.Milliseconds = sample.Milliseconds,
				// By name, so the same zone keeps its history across frames even
				// though the view into this frame's name pool does not.
				.RecentMaxMilliseconds = FrameGraph::GetRecentMax(sample.Name),
				.Category = ProfilerZoneCategories[index],
				.Collapsed = ProfilerZoneHasChildren[index] == 1,
			});
		}
	}

	namespace {
		// A counter's family: its name without the last segment, so every
		// primitive's part count is measured against the biggest part count
		// rather than against a triangle count three orders of magnitude away.
		std::string_view CounterGroup(std::string_view name) {
			size_t dot = name.rfind('.');
			return dot == std::string_view::npos ? std::string_view() : name.substr(0, dot);
		}
	} // namespace

	void Engine::BuildCounterRows() {
		const auto &counters = FrameGraph::GetCounters();

		Profiler.Counters.clear();
		Profiler.Counters.reserve(counters.size());
		for (const auto &counter : counters) {
			std::string_view name = counter.Name ? counter.Name : "";
			Profiler.Counters.push_back({
				.Name = name,
				.Value = counter.Value,
				.Samples = counter.Samples,
				.IsTime = counter.IsTime,
				.Category = ClassifyZone(name) == ProfilerCategory::Count ? ProfilerCategory::Engine
																		 : ClassifyZone(name),
				.Share = 0.0f,
			});
		}

		// Quadratic over a few dozen rows, five times a second. A map keyed on
		// the group would be more code than the loop it replaced.
		for (auto &row : Profiler.Counters) {
			std::string_view group = CounterGroup(row.Name);
			double largest = 0.0;
			size_t members = 0;

			for (const auto &other : Profiler.Counters) {
				if (CounterGroup(other.Name) != group || other.IsTime != row.IsTime) {
					continue;
				}
				members++;
				largest = std::max(largest, other.Value);
			}

			// A bar against nothing but itself is a full bar every time, which
			// says nothing. One of a kind gets no bar.
			row.Share = members > 1 && largest > 0.0 ? (float)(row.Value / largest) : 0.0f;
		}
	}

	void Engine::CountWorld() {
		G_PROFILE("debug.counters");

		// Counter names are kept by address, so a table built once rather than a
		// string formatted per frame. The id is Visual.MeshId: the PartType
		// index plus one, with 0 meaning a part that never had a shape set.
		static const auto NAMES = [] {
			std::array<std::pair<std::string, std::string>, 256> names;
			for (size_t id = 0; id < names.size(); id++) {
				auto shape = id > 0 ? magic_enum::enum_cast<Enums::PartType>((int)id - 1) : std::nullopt;
				std::string label = shape ? std::string(magic_enum::enum_name(*shape)) : std::string("Unset");
				names[id] = {"world.parts." + label, "world.tris." + label};
			}
			return names;
		}();

		auto &parts = Workspace->Parts;

		std::array<uint64_t, 256> partCounts{};
		std::array<uint64_t, 256> triangleCounts{};
		// Triangles behind a mesh id, resolved off the first part that uses one.
		// A mesh id is what says which mesh a part draws, so every part sharing
		// an id shares a triangle count -- and GetMesh builds a string and hashes
		// it, which is not something to do thirty thousand times a frame.
		std::array<uint32_t, 256> perId{};
		std::array<bool, 256> resolved{};
		uint64_t triangles = 0;

		uint32_t count = parts.Size();
		for (uint32_t index = 0; index < count; index++) {
			BasePart *part = parts.At(index);
			if (!part) {
				continue;
			}

			uint8_t id = part->Visual.MeshId;
			partCounts[id]++;

			if (!resolved[id]) {
				const std::unique_ptr<GpuMesh> &mesh = part->GetMesh();
				perId[id] = mesh ? mesh->IndexCount / 3 : 0;
				resolved[id] = true;
			}

			triangleCounts[id] += perId[id];
			triangles += perId[id];
		}

		ProfilerCount("world.objects", count);
		ProfilerCount("world.triangles", triangles);
		for (size_t id = 0; id < partCounts.size(); id++) {
			if (partCounts[id] == 0) {
				continue;
			}
			ProfilerCount(NAMES[id].first.c_str(), partCounts[id]);
			ProfilerCount(NAMES[id].second.c_str(), triangleCounts[id]);
		}

		// Straight onto the view as well: these two head the tab, and reading
		// them back out of the counter list would be the long way round.
		Profiler.TotalObjects = count;
		Profiler.TotalTriangles = triangles;
	}

	// True when the key belonged to the panel. The arrows are the game's the
	// rest of the time, so a panel that is not open must not eat them.
	bool Engine::StepProfilerKey(const SDL_Event &event) {
		if (event.type != SDL_EVENT_KEY_DOWN) {
			return false;
		}

		// Held down, these would repeat and flicker the panel on and off
		if (!event.key.repeat && event.key.key == SDLK_F3) {
			ShowStatistics = !ShowStatistics;
			SDL_Log("F3: statistics %s", ShowStatistics ? "on" : "off");
			return true;
		}
		if (!event.key.repeat && event.key.key == SDLK_F5) {
			ShowSystemTimings = !ShowSystemTimings;
			SDL_Log(
				"F5: profiler %s, %.*s tab",
				ShowSystemTimings ? "on" : "off",
				(int)GetProfilerTabName(Profiler.Tab).size(),
				GetProfilerTabName(Profiler.Tab).data()
			);
			return true;
		}

		// Below the F5 toggle, so everything from here needs the panel open. The
		// history only accumulates while recording is on, and recording is on
		// exactly while the panel is.
		if (!ShowSystemTimings) {
			return false;
		}

		if (!event.key.repeat && event.key.key == SDLK_F6) {
			WriteProfilerSnapshot();
			return true;
		}

		auto tabs = (int)ProfilerTab::Count;
		switch (event.key.key) {
		case SDLK_LEFT:
		case SDLK_RIGHT: {
			if (event.key.repeat) {
				return true;
			}

			// Backwards is one short of a full turn, so both directions are the
			// same line of arithmetic and neither can go negative.
			int step = event.key.key == SDLK_RIGHT ? 1 : tabs - 1;
			Profiler.Tab = (ProfilerTab)(((int)Profiler.Tab + step) % tabs);
			// Each tab is a different length, and carrying the offset over lands
			// halfway down a list nobody has read the top of yet.
			Profiler.Scroll = 0;
			SDL_Log(
				"F5: %.*s tab",
				(int)GetProfilerTabName(Profiler.Tab).size(),
				GetProfilerTabName(Profiler.Tab).data()
			);
			return true;
		}
		// Q and E shallower and deeper. Only the category tabs act on it, but the
		// setting is kept whichever tab is open so arrowing across and back does
		// not reset it.
		case SDLK_Q:
		case SDLK_E: {
			if (event.key.repeat) {
				return true;
			}

			uint32_t before = Profiler.DepthLimit;
			if (event.key.key == SDLK_E) {
				Profiler.DepthLimit = std::min(Profiler.DepthLimit + 1, ProfilerView::MAXIMUM_DEPTH_LIMIT);
			} else if (Profiler.DepthLimit > 0) {
				Profiler.DepthLimit--;
			}

			if (Profiler.DepthLimit != before) {
				// Fewer rows can leave the view scrolled past the end, and more
				// rows arriving under the cursor is disorienting either way.
				Profiler.Scroll = 0;
				SDL_Log("F5: depth %u", Profiler.DepthLimit);
			}
			return true;
		}
		// Repeats are allowed here: holding an arrow to scroll is the point.
		// The ceiling is applied by the draw, which is the only thing that
		// knows how many rows fit.
		case SDLK_UP: Profiler.Scroll = std::max(Profiler.Scroll - ProfilerView::SCROLL_STEP, 0); return true;
		case SDLK_DOWN: Profiler.Scroll += ProfilerView::SCROLL_STEP; return true;
		default: return false;
		}
	}

	// F6. Written to a file rather than shown: the point of it is the frames that
	// are not on screen, and a panel that fits forty rows cannot show five seconds
	// of them.
	void Engine::WriteProfilerSnapshot() {
		size_t frames = FrameGraph::GetHistoryFrames();
		if (frames == 0) {
			SDL_Log("F6: nothing retained yet -- the panel has to have been open for a frame to complete");
			return;
		}

		// Numbered by frame rather than by clock, so successive snapshots sort in
		// the order they were taken without a date format to read.
		std::string name = "profiler-snapshot-" + std::to_string(FramesRun) + ".txt";
		if (!FrameGraph::WriteSnapshot(name.c_str())) {
			SDL_Log("F6: could not write %s", name.c_str());
			return;
		}

		// Absolute, because the relative path is against the working directory and
		// that is not always where anybody thinks it is.
		std::error_code error;
		auto resolved = std::filesystem::absolute(name, error);
		SDL_Log(
			"F6: snapshot of %zu frames over %.2f s -> %s",
			frames,
			FrameGraph::GetHistorySeconds(),
			error ? name.c_str() : resolved.string().c_str()
		);
	}

	void Engine::UpdateStatistics(double now, float deltaTime) {
		// Recorded even while hidden: it is turned on because something went
		// wrong a moment ago. Except across a resume, which is an artefact of
		// the window having been away.
		if (now >= SettleUntil) {
			Statistics.Record(now, deltaTime);
		}

		// Recording costs two clock reads per zone, so it is on only while the
		// panel that shows it is open.
		FrameGraph::SetEnabled(ShowSystemTimings);

		// Every frame rather than every repaint: the panel reads the last
		// completed frame, and a frame that skipped the walk has no counters in
		// it to read.
		if (ShowSystemTimings && Profiler.Tab == ProfilerTab::Counters) {
			CountWorld();
		}

		if (!ShowStatistics && !ShowSystemTimings) {
			// The panel itself is kept, so turning it back on is free
			RenderProvider->SetWindowOverlay(0, nullptr, glm::vec2(0.0f));
			return;
		}

		if (!StatisticsPanel) {
			StatisticsPanel = std::make_shared<EditableImage>();
			StatisticsPanel->Name = EditableImage::DEFINITION.Name;
			// Nothing to draw from yet, so paint it now rather than leaving a
			// blank rectangle on screen until the first refresh comes round
			LastStatisticsRefresh = 0.0;
		}

		if (LastStatisticsRefresh == 0.0 || now - LastStatisticsRefresh >= STATISTICS_REFRESH_SECONDS) {
			// Drawn into a plain pixel buffer and handed to the renderer as an
			// image. The panel drawing has no business knowing about a
			// Luau-facing class, and the renderer's overlay indices take one.
			if (ShowSystemTimings) {
				BuildProfilerView();
				BuildCounterRows();

				// Whatever the window is, less the margin it sits in and a little
				// air at the bottom. Without this the Full tab is taller than the
				// window it is drawn over.
				int windowHeight = 0;
				SDL_GetWindowSizeInPixels(Window, nullptr, &windowHeight);
				Profiler.MaximumHeight = std::max(160, windowHeight - (int)STATISTICS_MARGIN * 4);
			}

			DrawDebugPanels(
				OverlayBuffer,
				ShowStatistics ? &Statistics : nullptr,
				ShowSystemTimings ? &Profiler : nullptr,
				G_PROFILE_ACTIVE()
			);
			if (!OverlayBuffer.IsEmpty()) {
				StatisticsPanel->SetPixels(
					OverlayBuffer.GetWidth(), OverlayBuffer.GetHeight(), OverlayBuffer.GetPixels()
				);
			}
			LastStatisticsRefresh = now;
		}

		if (OverlayBuffer.IsEmpty()) {
			RenderProvider->SetWindowOverlay(0, nullptr, glm::vec2(0.0f));
			return;
		}

		RenderProvider->SetWindowOverlay(0, StatisticsPanel, glm::vec2(STATISTICS_MARGIN, STATISTICS_MARGIN));
	}

	void Engine::Step() {
		if (!IsRunning) {
			return;
		}

		CurrentTick = SDL_GetTicksNS();
		if (LastTick == 0) {
			LastTick = CurrentTick;
		}
		float deltaTime = GetDeltaTime();
		LastTick = CurrentTick;

		double seconds = (double)CurrentTick / 1000000000.0;

		FrameSeconds = seconds;
		FrameGraph::BeginFrame();
		{
			G_PROFILE("Main Thread");
			Scheduler.Run(deltaTime);
		}
		FrameGraph::EndFrame();

		// Outside the zone: it separates frames rather than belonging to one
		G_PROFILE_FRAME();

		FramesRun++;
		if (MaximumFrames >= 0 && (int64_t)FramesRun >= MaximumFrames) {
			SDL_Log("Ran %llu frames, stopping", (unsigned long long)FramesRun);
			IsRunning = false;
		}

		PaceSimulation();
	}

	void Engine::PaceSimulation() {
		double interval = RenderSettings->GetSimulationInterval();
		if (interval <= 0.0) {
			return;
		}

		uint64_t spent = SDL_GetTicksNS() - CurrentTick;
		auto budget = (uint64_t)(interval * 1000000000.0);
		if (spent >= budget) {
			return;
		}

		SDL_DelayNS(budget - spent);
	}

	// Phase order, then declaration order inside a phase. Nothing here names
	// another system as a dependency: if the order has to change, a system
	// moves phase rather than growing a reference to its neighbour.
	void Engine::RegisterSystems() {
		using ecs::Phase;

		Scheduler.Add(Phase::OnLoad, "input.poll", [this](float deltaTime) {
			StepEvents(deltaTime, FrameSeconds);
		});

		// Ahead of the frame's work, so the reading belongs to the frame just
		// measured rather than the one about to start.
		Scheduler.Add(Phase::PreUpdate, "debug.panels", [this](float deltaTime) {
			StepStatistics(deltaTime, FrameSeconds);
		});

		Scheduler.Add(Phase::OnUpdate, "simulation.step", [this](float deltaTime) { StepSimulation(deltaTime); });
		Scheduler.Add(Phase::OnUpdate, "physics.step", [this](float deltaTime) { Workspace->StepPhysics(deltaTime); });

		// Only the lights whose part moved, off the parts' change channel.

		Scheduler.Add(Phase::PreStore, "gpu.wait", [this](float) {
			if (!IsRunning) return;
			StepGpuWait();
		});

		Scheduler.Add(Phase::OnStore, "render.draw", [this](float deltaTime) {
			if (!IsRunning) return;
			StepRender(deltaTime, FrameSeconds);
		});

		Scheduler.Add(Phase::OnStore, "script.step", [this](float) { StepScripts(); });
	}

	// StepFrame is gone: its stages are systems now, registered in
	// RegisterSystems and run by the scheduler in phase order. Each keeps
	// the Tracy zone it always had, so the trace reads the same.
	void Engine::StepEvents(float deltaTime, double seconds) {
		SDL_Event event;
		{
		G_PROFILE("Events");
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				IsRunning = false;
				return;
			}

			// Timings across the gap belong to the gap, not to the engine
			if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_RESTORED ||
				event.type == SDL_EVENT_WINDOW_SHOWN || event.type == SDL_EVENT_WINDOW_EXPOSED) {
				SettleUntil = seconds + SETTLE_AFTER_RESUME;
			}

			// Swallowed rather than forwarded: while the panel is open its
			// arrows are its own, and a game that binds them should not also
			// walk sideways every time a tab is changed.
			if (StepProfilerKey(event)) {
				continue;
			}

			UserInputService->ProcessEvent(event);
			Workspace->CurrentCamera->OnEvent(Window, event);
		}
		}

	}

	void Engine::StepStatistics(float deltaTime, double seconds) {
		// Ahead of the frame's work, so the reading belongs to the frame just
		// measured
		UpdateStatistics(seconds, deltaTime);
		UpdateAutomaticProfile(seconds);

	}

	void Engine::StepSimulation(float deltaTime) {
		{
			G_PROFILE("Simulation");
			RunService->FireSimulation(deltaTime);
			{
				// Between the two halves of the step, and not a signal, so it is
				// the one part of this the panel would otherwise attribute to
				// whichever script ran either side of it.
				G_PROFILE("camera.step");
				Workspace->CurrentCamera->Step(deltaTime);
			}
			Workspace->DistributedGameTime += deltaTime;
			RunService->FirePostSimulation(deltaTime);
		}

		{
			G_PROFILE("PreRender");
			RunService->FireRender(deltaTime);
		}
		{
			// Tweens settle right before the frame is drawn, so the values
			// written this step are the ones rendered
			G_PROFILE("Tweens");
			TweenService->Step(deltaTime);
		}
	}

	// Paces this frame against the GPU before any of it is submitted. Nothing
	// else in a frame waits, so without this the backlog of submitted-but-
	// unfinished work grows without bound.
	//
	// Its own system rather than the first few lines of the render one, because
	// it is the only place a frame waits: folded into the draw it made CPU work
	// and GPU work look like one number, and cutting CPU work out of a
	// GPU-bound frame then looked like it had achieved nothing.
	// RenderSettings.BufferCount, handed to SDL. Double buffering at 2, triple at 3.
	//
	// Only when it changes, and that is not an optimisation: SDL stalls and flushes
	// the whole command queue to apply it, so calling it every frame would be a
	// full pipeline drain every frame.
	void Engine::ApplyBufferCount() {
		int wanted = RenderSettings->GetBufferCount();
		if (wanted == AppliedBufferCount || !Gpu) {
			return;
		}

		if (!SDL_SetGPUAllowedFramesInFlight(Gpu, (Uint32)wanted)) {
			SDL_Log("buffers: could not set %d: %s", wanted, SDL_GetError());
			// Not retried every frame. Whatever SDL is still using is what the
			// engine has, and saying so once beats a line a frame.
			AppliedBufferCount = wanted;
			return;
		}

		SDL_Log("buffers: %d (%s)", wanted, wanted == 1 ? "single" : wanted == 2 ? "double" : "triple");
		AppliedBufferCount = wanted;
	}

	void Engine::StepGpuWait() {
		G_PROFILE("GPU Wait");
		ApplyBufferCount();
		// This is where the frame waits: past BufferCount frames pending,
		// SDL_WaitAndAcquireGPUSwapchainTexture blocks.
		RenderProvider->BeginFrame(RenderSettings->GetFramesInFlight());
	}

	void Engine::StepRender(float deltaTime, double seconds) {
		{
			G_PROFILE("Mesh Upload");
			MeshProvider::UploadToGpu(Gpu);
		}

		// Read every frame, so swapping the pass takes effect on the next one
		// rather than needing the renderer restarted
		RenderProvider->SetAntialiasOverride(RenderSettings->GetAntialiasShader());

		auto worldRoot = std::static_pointer_cast<WorldRoot>(Workspace);
		auto lightDirection = Lighting->GetSunDirection();

		// Published so an ad-hoc Camera:Render() draws this same world
		RenderProvider->Scene.WorldRoot = worldRoot;
		RenderProvider->Scene.LightDirection = lightDirection;
		RenderProvider->Scene.TimeSeconds = Workspace->DistributedGameTime;
		// Cameras compare this against the one they last drew at, so a scene
		// that has not moved is not redrawn
		{
			G_PROFILE("Scene Signature");
			RenderProvider->UpdateSceneSignature(worldRoot, lightDirection);
		}

		auto currentCamera = Workspace->CurrentCamera;

		// A camera drawn to the window renders into an offscreen target first,
		// and that target is sized from ViewportSize. Nothing filled it in:
		// Camera::OnEvent only sets it for a freecam, and only when a resize
		// event arrives, so a window that was never resized left it at zero,
		// AcquireCameraTarget refused a zero-sized target, and the whole draw
		// bailed out having rendered nothing at all.
		int windowWidth = 0, windowHeight = 0;
		SDL_GetWindowSizeInPixels(Window, &windowWidth, &windowHeight);
		if (windowWidth > 0 && windowHeight > 0) {
			auto fitToWindow = [&](const std::shared_ptr<Camera> &camera) {
				if (!camera) {
					return;
				}

				// Its own share of the window, so a split-screen pane renders at
				// the size it is about to occupy rather than the whole width
				auto region = RenderProvider::ComputeWindowRegion(*camera, windowWidth, windowHeight);
				if (region.Width > 0 && region.Height > 0) {
					camera->ViewportSize = Vector2((float)region.Width, (float)region.Height);
				}
			};

			fitToWindow(currentCamera);
			for (auto *camera : Camera::GetAllCameras()) {
				if (!camera->DrawToWindow) {
					continue;
				}
				if (auto owned = camera->weak_from_this().lock()) {
					fitToWindow(std::static_pointer_cast<Camera>(owned));
				}
			}
		}

		double windowNow = Workspace->DistributedGameTime;
		float windowMaximumFps = RenderSettings->GetMaxCameraFPS();

		// Only offscreen cameras used to have a rate. A pane showing a corner
		// of the world need not be as current as the view being looked through.
		auto isDue = [&](const std::shared_ptr<Camera> &camera) {
			double interval = camera->GetRenderInterval(windowMaximumFps);
			// A pane that has never drawn has nothing to show
			if (interval <= 0.0 || camera->LastOffscreenDraw < 0.0) {
				camera->LastOffscreenDraw = windowNow;
				return true;
			}

			if (windowNow - camera->LastOffscreenDraw < interval) {
				return false;
			}

			camera->LastOffscreenDraw = windowNow;
			return true;
		};

		// Anything that draws into the window, CurrentCamera first so it takes
		// the whole thing when nothing else asks for a share
		std::vector<DrawContext> windowCameras;
		std::vector<DrawContext> offscreenCameras;
		{
		G_PROFILE("Camera Lists");
		if (currentCamera && currentCamera->Enabled) {
			windowCameras.push_back({
				.WorldRoot = worldRoot,
				.Camera = currentCamera,
				.LightDirection = lightDirection,
				.ShouldSkipRedraw = !isDue(currentCamera),
			});
		}

		// Everything that renders on its own this frame
		std::vector<Camera *> offscreenRoots;
		for (auto *camera : Camera::GetAllCameras()) {
			if (!camera->Enabled || camera == currentCamera.get()) {
				continue;
			}

			if (camera->DrawToWindow) {
				// A Camera that is not owned by a shared_ptr cannot be handed
				// to the renderer, so skip it rather than throwing bad_weak_ptr
				if (auto owned = camera->weak_from_this().lock()) {
					auto shared = std::static_pointer_cast<Camera>(owned);
					windowCameras.push_back({
						.WorldRoot = worldRoot,
						.Camera = shared,
						.LightDirection = lightDirection,
						.ShouldSkipRedraw = !isDue(shared),
					});
				}
			} else {
				offscreenRoots.push_back(camera);
			}
		}

		// Anything a window camera samples must also be up to date before the
		// window is drawn
		for (const auto &context : windowCameras) {
			for (Camera *dependency : RenderProvider->GetDirectlySampledCameras(context.Camera.get())) {
				offscreenRoots.push_back(dependency);
			}
		}

		for (Camera *camera : RenderProvider->GetSurfaceCameras()) {
			offscreenRoots.push_back(camera);
		}

		std::vector<Camera *> viewers;
		viewers.reserve(windowCameras.size());
		for (const auto &context : windowCameras) {
			viewers.push_back(context.Camera.get());
		}

		const auto &demanded =
			RenderProvider->GetDemandedCameras(viewers, RenderSettings->GetCameraVisibilityMargin());
		double idleInterval = RenderSettings->GetIdleCameraInterval();

		// Sorted so a camera reading another's target sees this frame's picture.
		// Enabled decides whether a camera renders on its own; one that another
		// camera samples is drawn regardless, or that camera would be wrong.
		// A camera feeding a texture rarely needs to be as current as the view
		// the player is looking at, so it redraws at its own rate. Skipping
		// leaves the last picture in its target, which is what a reader samples
		// and what a slow security feed should look like anyway.
		double now = Workspace->DistributedGameTime;
		float maximumCameraFps = RenderSettings->GetMaxCameraFPS();

		for (Camera *camera : RenderProvider->GetRenderOrder(offscreenRoots)) {
			auto owned = camera->weak_from_this().lock();
			if (!owned) {
				continue;
			}

			double interval = camera->GetRenderInterval(maximumCameraFps);

			// On demand: nothing here draws it. Camera:Render() still draws it
			// and everything it samples, which is the point of the mode.
			if (interval < 0.0) {
				continue;
			}

			if (idleInterval >= 0.0 && !demanded.count(camera)) {
				interval = glm::max(interval, idleInterval);
			}

			// A camera that has never drawn always draws, or its target would
			// be blank until the first interval elapsed
			if (interval > 0.0 && camera->LastOffscreenDraw >= 0.0 &&
				now - camera->LastOffscreenDraw < interval) {
				continue;
			}
			camera->LastOffscreenDraw = now;

			offscreenCameras.push_back({
				.WorldRoot = worldRoot,
				.Camera = std::static_pointer_cast<Camera>(owned),
				.LightDirection = lightDirection,
			});
		}
		}
		{
			// One command buffer for the lot, still in dependency order
			G_PROFILE("Offscreen Cameras");
			RenderProvider->DrawOffscreen(offscreenCameras);
		}

		{
			G_PROFILE("Window");
			if (windowCameras.size() == 1) {
				// One camera filling the window is the common case and can draw
				// straight into the swapchain
				RenderProvider->Draw(windowCameras.front());
			} else if (!windowCameras.empty()) {
				RenderProvider->DrawComposite(windowCameras);
			}
		}

		RenderProvider->EndFrame();

	}

	void Engine::StepScripts() {
		G_PROFILE("Scripts");
		{
			// Resume any script waiting on a Camera:Render() readback. Its own
			// zone because the resumed script runs inside it: a readback that
			// wakes an expensive handler is not a slow readback.
			G_PROFILE("luau.renderPolls");
			RenderProvider->ResumeCompletedReadbacks(&ScriptEngine->ThreadEngine);
		}
		ScriptEngine->Step();
	}
} // namespace gargantuan
