#include "gargantuan/DebugOverlay.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace gargantuan {
	namespace {
		constexpr int SCALE = 2;
		constexpr int PADDING = 8;
		constexpr int LINE = DebugText::GLYPH_HEIGHT * SCALE + 4;
		// Tall enough for scale-two text with a little room either side
		constexpr int BAR = DebugText::GLYPH_HEIGHT * SCALE + 5;
		constexpr int PANEL_WIDTH = 760;
		constexpr int CHART_WIDTH = PANEL_WIDTH - PADDING * 2;
		// A bar narrower than this has nowhere to put even one character, so it
		// is drawn as a block of colour and left unlabelled
		constexpr float LABEL_MINIMUM = 26.0f;

		const Color3 INK = Color3::fromRGB(236, 240, 248);
		const Color3 DIM = Color3::fromRGB(150, 160, 185);
		const Color3 ACCENT = Color3::fromRGB(150, 210, 255);

		std::string Fixed(double value, int places) {
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f", places, value);
			return buffer;
		}

		// Hue from the name, so a zone keeps its colour from one second to the
		// next and the eye can follow a row without reading it
		Color3 ZoneColour(std::string_view name, int depth) {
			uint32_t hash = 2166136261u;
			for (char character : name) {
				hash = (hash ^ (uint32_t)(unsigned char)character) * 16777619u;
			}

			// Deeper rows are lighter, so nesting reads as depth even where two
			// zones landed on similar hues
			float value = std::min(0.52f + (float)depth * 0.08f, 0.86f);
			return Color3::fromHSV((float)(hash % 360) / 360.0f, 0.55f, value);
		}

		double Share(const Profiler::Snapshot &snapshot, double milliseconds) {
			if (snapshot.FrameMilliseconds <= 0.0) {
				return 0.0;
			}
			return std::clamp(milliseconds / snapshot.FrameMilliseconds, 0.0, 1.0);
		}

		int DeepestBelow(const Profiler::Snapshot &snapshot, size_t index) {
			const Profiler::Zone &zone = snapshot.Zones[index];
			int deepest = zone.Depth;
			for (size_t child : zone.Children) {
				deepest = std::max(deepest, DeepestBelow(snapshot, child));
			}
			return deepest;
		}

		void DrawZone(
			EditableImage &image,
			const Profiler::Snapshot &snapshot,
			size_t index,
			float left,
			float top,
			int rootDepth
		) {
			const Profiler::Zone &zone = snapshot.Zones[index];
			float width = (float)(Share(snapshot, zone.Milliseconds) * CHART_WIDTH);
			float y = top + (float)((zone.Depth - rootDepth) * BAR);

			if (width >= 1.0f) {
				image.DrawRectangle(
					Vector2(left, y),
					Vector2(std::max(width - 1.0f, 1.0f), (float)(BAR - 1)),
					ZoneColour(zone.Name, zone.Depth - rootDepth),
					0.0f,
					Enums::ImageCombineType::Overwrite
				);

				if (width >= LABEL_MINIMUM) {
					// However much of the name fits, rather than a name that
					// runs out of its own bar and into the next one
					int room = (int)((width - 6.0f) / (DebugText::ADVANCE * SCALE));
					std::string label = zone.Name.substr(0, (size_t)std::max(room, 0));
					DebugText::Draw(
						image,
						Vector2(left + 3.0f, y + 2.0f),
						label,
						Color3::fromRGB(16, 16, 22),
						0.0f,
						SCALE
					);
				}
			}

			// Laid end to end inside the parent. Whatever is left over at the
			// right is the parent's own time, which is the gap that makes a
			// flame chart worth reading.
			float childLeft = left;
			for (size_t child : zone.Children) {
				DrawZone(image, snapshot, child, childLeft, top, rootDepth);
				childLeft += (float)(Share(snapshot, snapshot.Zones[child].Milliseconds) * CHART_WIDTH);
			}
		}
	} // namespace

	ProfilerPanelLayout DrawProfilerPanel(
		EditableImage &image, const Profiler::Snapshot &snapshot, std::string_view status
	) {
		ProfilerPanelLayout layout;

		// Worked out before anything is drawn, because the image has to be the
		// right size before the first rectangle lands in it
		int height = PADDING + LINE;
		for (size_t root : snapshot.Roots) {
			height += LINE;
			height += (DeepestBelow(snapshot, root) - snapshot.Zones[root].Depth + 1) * BAR;
			height += 4;
		}

		int counterRows = ((int)snapshot.Counters.size() + 2) / 3;
		if (counterRows > 0) {
			height += LINE + counterRows * LINE;
		}
		// The status line and the export button share the last band
		height += LINE + PADDING + 6;

		Vector2 size((float)PANEL_WIDTH, (float)height);
		if (image.GetWidth() != PANEL_WIDTH || image.GetHeight() != height) {
			image.Resize(size);
		}
		layout.Size = size;

		image.DrawRectangle(
			Vector2(0, 0), size, Color3::fromRGB(10, 10, 15), 0.12f, Enums::ImageCombineType::Overwrite
		);

		float y = (float)PADDING;

		std::string header = "PROFILER  " + Fixed(snapshot.FrameMilliseconds, 2) + " MS FRAME  " +
			std::to_string(snapshot.Frames) + " FRAMES OVER " + Fixed(snapshot.Seconds, 1) + "S";
		DebugText::Draw(image, Vector2((float)PADDING, y), header, INK, 0.0f, SCALE);
		y += (float)LINE;

		for (size_t root : snapshot.Roots) {
			const Profiler::Zone &zone = snapshot.Zones[root];

			std::string title = zone.Name + "  " + Fixed(zone.Milliseconds, 2) + " MS  " +
				Fixed(Share(snapshot, zone.Milliseconds) * 100.0, 0) + "%";
			DebugText::Draw(image, Vector2((float)PADDING, y), title, ACCENT, 0.0f, SCALE);
			y += (float)LINE;

			DrawZone(image, snapshot, root, (float)PADDING, y, zone.Depth);
			y += (float)((DeepestBelow(snapshot, root) - zone.Depth + 1) * BAR + 4);
		}

		if (!snapshot.Counters.empty()) {
			DebugText::Draw(image, Vector2((float)PADDING, y), "COUNTS PER FRAME", ACCENT, 0.0f, SCALE);
			y += (float)LINE;

			// Three to a row, because a counter is a short name and a small
			// number and one per line would be mostly empty panel
			int column = 0;
			float rowTop = y;
			for (const auto &counter : snapshot.Counters) {
				std::string text = counter.Name + " " + Fixed(counter.PerFrame, 1);
				DebugText::Draw(
					image,
					Vector2((float)(PADDING + column * (CHART_WIDTH / 3)), rowTop),
					text,
					DIM,
					0.0f,
					SCALE
				);

				if (++column == 3) {
					column = 0;
					rowTop += (float)LINE;
				}
			}
			y = rowTop + (column == 0 ? 0.0f : (float)LINE);
		}

		// The button, and the status line to the left of it
		float buttonWidth = (float)(DebugText::Measure("EXPORT", SCALE) + 14);
		float buttonHeight = (float)(DebugText::GLYPH_HEIGHT * SCALE + 8);
		Vector2 buttonPosition((float)(PANEL_WIDTH - PADDING) - buttonWidth, y);

		image.DrawRectangle(
			buttonPosition,
			Vector2(buttonWidth, buttonHeight),
			Color3::fromRGB(64, 108, 156),
			0.0f,
			Enums::ImageCombineType::Overwrite
		);
		DebugText::Draw(
			image,
			Vector2(buttonPosition.GetX() + 7.0f, buttonPosition.GetY() + 4.0f),
			"EXPORT",
			Color3::fromRGB(12, 14, 20),
			0.0f,
			SCALE
		);

		if (!status.empty()) {
			DebugText::Draw(image, Vector2((float)PADDING, y + 3.0f), status, DIM, 0.0f, SCALE);
		}

		layout.ButtonPosition = buttonPosition;
		layout.ButtonSize = Vector2(buttonWidth, buttonHeight);
		return layout;
	}
} // namespace gargantuan
