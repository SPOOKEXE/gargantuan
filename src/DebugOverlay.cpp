#include "gargantuan/DebugOverlay.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace gargantuan {
	void OverlayImage::Resize(int width, int height) {
		Width = std::max(width, 0);
		Height = std::max(height, 0);
		Pixels.assign((size_t)Width * (size_t)Height * 4, 0);
	}

	void OverlayImage::Blend(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
		if (IsEmpty() || alpha == 0) {
			return;
		}

		// Clipped rather than asserted: the panel is laid out in text units and
		// a glyph on the last line can round past the edge by a pixel.
		int left = std::max(x, 0);
		int top = std::max(y, 0);
		int right = std::min(x + width, Width);
		int bottom = std::min(y + height, Height);

		for (int row = top; row < bottom; row++) {
			for (int column = left; column < right; column++) {
				uint8_t *pixel = &Pixels[((size_t)row * (size_t)Width + (size_t)column) * 4];

				// Straight alpha, source-over. The destination starts cleared,
				// so the first write into a texel is just the source.
				int inverse = 255 - alpha;
				pixel[0] = (uint8_t)((red * alpha + pixel[0] * inverse) / 255);
				pixel[1] = (uint8_t)((green * alpha + pixel[1] * inverse) / 255);
				pixel[2] = (uint8_t)((blue * alpha + pixel[2] * inverse) / 255);
				pixel[3] = (uint8_t)(alpha + pixel[3] * inverse / 255);
			}
		}
	}

	void FrameStatistics::Record(double now, float deltaTime) {
		// A zero-length frame is the clock not having moved yet rather than an
		// infinitely fast one, and dividing by it would poison every maximum
		// after it
		if (deltaTime <= 0.0f) {
			return;
		}

		Samples.push_back({now, deltaTime});

		while (!Samples.empty() && now - Samples.front().Time > WINDOW_SECONDS) {
			Samples.pop_front();
		}
	}

	bool FrameStatistics::HasSamples() const {
		return !Samples.empty();
	}

	void FrameStatistics::Clear() {
		Samples.clear();
	}

	float FrameStatistics::Current() const {
		if (Samples.empty()) {
			return 0.0f;
		}
		return 1.0f / Samples.back().Delta;
	}

	float FrameStatistics::Minimum() const {
		if (Samples.empty()) {
			return 0.0f;
		}

		// Longest frame, lowest rate: a maximum until it becomes a rate
		float longest = Samples.front().Delta;
		for (const auto &sample : Samples) {
			longest = std::max(longest, sample.Delta);
		}
		return 1.0f / longest;
	}

	float FrameStatistics::Maximum() const {
		if (Samples.empty()) {
			return 0.0f;
		}

		float shortest = Samples.front().Delta;
		for (const auto &sample : Samples) {
			shortest = std::min(shortest, sample.Delta);
		}
		return 1.0f / shortest;
	}

	float FrameStatistics::Average() const {
		if (Samples.empty()) {
			return 0.0f;
		}

		double total = 0.0;
		for (const auto &sample : Samples) {
			total += sample.Delta;
		}
		if (total <= 0.0) {
			return 0.0f;
		}

		// Frames over the time they took, not the mean of the rates
		return (float)((double)Samples.size() / total);
	}

	namespace DebugText {
		namespace {
			// Five rows of three bits, most significant leftmost. Written out
			// because at this size every glyph is a judgement call.
			struct Glyph {
				char Character;
				std::array<uint8_t, GLYPH_HEIGHT> Rows;
			};

			constexpr std::array<Glyph, 47> GLYPHS = {{
				{'0', {0b111, 0b101, 0b101, 0b101, 0b111}},
				{'1', {0b010, 0b110, 0b010, 0b010, 0b111}},
				{'2', {0b111, 0b001, 0b111, 0b100, 0b111}},
				{'3', {0b111, 0b001, 0b111, 0b001, 0b111}},
				{'4', {0b101, 0b101, 0b111, 0b001, 0b001}},
				{'5', {0b111, 0b100, 0b111, 0b001, 0b111}},
				{'6', {0b111, 0b100, 0b111, 0b101, 0b111}},
				{'7', {0b111, 0b001, 0b001, 0b001, 0b001}},
				{'8', {0b111, 0b101, 0b111, 0b101, 0b111}},
				{'9', {0b111, 0b101, 0b111, 0b001, 0b111}},
				{'A', {0b111, 0b101, 0b111, 0b101, 0b101}},
				{'B', {0b110, 0b101, 0b110, 0b101, 0b110}},
				{'C', {0b111, 0b100, 0b100, 0b100, 0b111}},
				{'D', {0b110, 0b101, 0b101, 0b101, 0b110}},
				{'E', {0b111, 0b100, 0b111, 0b100, 0b111}},
				{'F', {0b111, 0b100, 0b111, 0b100, 0b100}},
				{'G', {0b111, 0b100, 0b101, 0b101, 0b111}},
				{'H', {0b101, 0b101, 0b111, 0b101, 0b101}},
				{'I', {0b111, 0b010, 0b010, 0b010, 0b111}},
				{'J', {0b001, 0b001, 0b001, 0b101, 0b111}},
				{'K', {0b101, 0b101, 0b110, 0b101, 0b101}},
				{'L', {0b100, 0b100, 0b100, 0b100, 0b111}},
				{'M', {0b101, 0b111, 0b111, 0b101, 0b101}},
				{'N', {0b110, 0b101, 0b101, 0b101, 0b101}},
				{'O', {0b111, 0b101, 0b101, 0b101, 0b111}},
				{'P', {0b111, 0b101, 0b111, 0b100, 0b100}},
				{'Q', {0b111, 0b101, 0b101, 0b111, 0b011}},
				{'R', {0b111, 0b101, 0b110, 0b101, 0b101}},
				{'S', {0b111, 0b100, 0b111, 0b001, 0b111}},
				{'T', {0b111, 0b010, 0b010, 0b010, 0b010}},
				{'U', {0b101, 0b101, 0b101, 0b101, 0b111}},
				{'V', {0b101, 0b101, 0b101, 0b101, 0b010}},
				{'W', {0b101, 0b101, 0b111, 0b111, 0b101}},
				{'X', {0b101, 0b101, 0b010, 0b101, 0b101}},
				{'Y', {0b101, 0b101, 0b010, 0b010, 0b010}},
				{'Z', {0b111, 0b001, 0b010, 0b100, 0b111}},
				{'.', {0b000, 0b000, 0b000, 0b000, 0b010}},
				// System names are dotted paths with a separator the font would
				// otherwise drop, and a gap reads as two names
				{'-', {0b000, 0b000, 0b111, 0b000, 0b000}},
				// The profiler's own punctuation: a share is meaningless without
				// its sign, a collapsed row is marked with one, and the tab strip
				// points at the keys that move it
				{'%', {0b101, 0b001, 0b010, 0b100, 0b101}},
				{'+', {0b000, 0b010, 0b111, 0b010, 0b000}},
				{':', {0b000, 0b010, 0b000, 0b010, 0b000}},
				{'/', {0b001, 0b001, 0b010, 0b100, 0b100}},
				{'<', {0b001, 0b010, 0b100, 0b010, 0b001}},
				{'>', {0b100, 0b010, 0b001, 0b010, 0b100}},
				{'(', {0b001, 0b010, 0b010, 0b010, 0b001}},
				{')', {0b100, 0b010, 0b010, 0b010, 0b100}},
				// Counts run to seven figures, and seven figures unbroken is a
				// number nobody reads, they only measure it against the one above
				{',', {0b000, 0b000, 0b000, 0b010, 0b100}},
			}};

			const Glyph *Find(char character) {
				// The font has one case, and a missing glyph is a hole
				if (character >= 'a' && character <= 'z') {
					character = (char)(character - 'a' + 'A');
				}

				for (const auto &glyph : GLYPHS) {
					if (glyph.Character == character) {
						return &glyph;
					}
				}
				return nullptr;
			}
		} // namespace

		int Measure(std::string_view text, int scale) {
			if (text.empty()) {
				return 0;
			}
			return (int)((text.size() * ADVANCE - 1) * (size_t)std::max(scale, 1));
		}

		void Draw(
			OverlayImage &image,
			int x,
			int y,
			std::string_view text,
			uint8_t red,
			uint8_t green,
			uint8_t blue,
			int scale
		) {
			scale = std::max(scale, 1);
			int pen = x;

			for (char character : text) {
				const Glyph *glyph = Find(character);
				// A space, or an unknown, advances without drawing
				if (glyph) {
					for (int row = 0; row < GLYPH_HEIGHT; row++) {
						uint8_t bits = glyph->Rows[(size_t)row];
						for (int column = 0; column < GLYPH_WIDTH; column++) {
							if ((bits & (1 << (GLYPH_WIDTH - 1 - column))) == 0) {
								continue;
							}

							image.Blend(
								pen + column * scale, y + row * scale, scale, scale, red, green, blue, 255
							);
						}
					}
				}

				pen += ADVANCE * scale;
			}
		}
	} // namespace DebugText

	namespace {
		constexpr int SCALE = 2;
		constexpr int PADDING = 6;
		constexpr int LINE_HEIGHT = DebugText::GLYPH_HEIGHT * SCALE + 4;

		struct Line {
			std::string Text;
			uint8_t Red = 255;
			uint8_t Green = 255;
			uint8_t Blue = 255;
		};

		// Capped at four digits: past that it is wider than the panel
		std::string Rate(float framesPerSecond) {
			int rounded = (int)(framesPerSecond + 0.5f);
			rounded = std::clamp(rounded, 0, 9999);

			char buffer[8];
			std::snprintf(buffer, sizeof(buffer), "%d", rounded);
			return buffer;
		}

		// Right-aligned so columns do not shuffle, and one wider than the
		// longest number or a four digit rate runs into its label
		std::string Column(std::string_view label, float framesPerSecond) {
			constexpr size_t FIELD = 5;

			std::string value = Rate(framesPerSecond);
			std::string padded(FIELD - std::min(value.size(), FIELD - 1), ' ');
			return std::string(label) + padded + value;
		}

		std::string Milliseconds(float value) {
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%.2f", std::clamp(value, 0.0f, 999.99f));
			return buffer;
		}
	} // namespace

	std::string_view GetProfilerTabName(ProfilerTab tab) {
		switch (tab) {
		case ProfilerTab::Main: return "MAIN";
		case ProfilerTab::Render: return "RENDER";
		case ProfilerTab::Physics: return "PHYSICS";
		case ProfilerTab::Luau: return "LUAU";
		case ProfilerTab::Full: return "FULL";
		case ProfilerTab::Counters: return "COUNTERS";
		default: return "?";
		}
	}

	namespace {
		// A row per zone, rather than the bar per zone this used to draw. Bars
		// that share a line save vertical space and cost every label, and the
		// label is the whole point: a wall of coloured rectangles says a frame
		// was busy without saying what it was busy with.
		constexpr int ROW_HEIGHT = 13;

		// Name, then this frame's time, then the recent worst, then share. Fixed
		// columns so the eye runs down the numbers instead of hunting for them past
		// names of different lengths.
		constexpr size_t NAME_FIELD = 30;
		constexpr size_t VALUE_FIELD = 6; // 999.99
		// Beside MS and in the same units, because the pair is the point: read
		// together they say "costs this much, except when it costs that much".
		constexpr size_t RMAX_FIELD = 6; // 999.99
		constexpr size_t SHARE_FIELD = 6; // 100.0%
		constexpr size_t ROW_CHARS = NAME_FIELD + 1 + VALUE_FIELD + 1 + RMAX_FIELD + 1 + SHARE_FIELD;

		// Where in the frame the zone ran, which the numbers cannot say: two
		// systems costing 2 ms each read the same whether they ran back to back
		// or with the GPU wait between them.
		constexpr int TIMELINE_WIDTH = 224;
		constexpr int TIMELINE_GAP = 10;

		// The colour chip that starts every row. Also what separates one line
		// from the next at a glance.
		constexpr int CHIP_WIDTH = 3;
		constexpr int CHIP_GAP = 5;

		// Two spaces a level, and no deeper: past this a row is more indent than
		// name, and the nesting is legible from the timeline anyway.
		constexpr uint32_t MAXIMUM_INDENT = 6;

		struct Colour {
			uint8_t Red, Green, Blue;
		};

		// One hue per part of the engine, so a tab is recognisable before it is
		// read and the Full tab is separable at all.
		constexpr Colour CATEGORY_COLOURS[(size_t)ProfilerCategory::Count] = {
			{214, 152, 74},	 // Engine
			{92, 160, 224},	 // Render
			{104, 196, 132}, // Physics
			{178, 138, 224}, // Luau
		};

		Colour CategoryColour(ProfilerCategory category) {
			auto index = (size_t)category;
			return CATEGORY_COLOURS[index < std::size(CATEGORY_COLOURS) ? index : 0];
		}

		// Deeper is dimmer, so a subtree reads as one thing shading away from
		// its root rather than as unrelated rows that happen to be adjacent.
		Colour Shade(Colour colour, uint32_t depth) {
			float scale = std::max(1.0f - (float)depth * 0.08f, 0.5f);
			return {
				(uint8_t)((float)colour.Red * scale),
				(uint8_t)((float)colour.Green * scale),
				(uint8_t)((float)colour.Blue * scale),
			};
		}

		int MeasureChars(size_t count) {
			return count == 0 ? 0 : (int)((count * DebugText::ADVANCE - 1) * SCALE);
		}

		std::string PadRight(std::string text, size_t width) {
			if (text.size() > width) {
				text.resize(width);
			}
			text.append(width - text.size(), ' ');
			return text;
		}

		std::string PadLeft(std::string text, size_t width) {
			if (text.size() >= width) {
				return text;
			}
			return std::string(width - text.size(), ' ') + text;
		}

		// Thousands separated. A part count and a triangle count sit in the same
		// column and differ by three orders of magnitude; unbroken, they read as
		// the same number until they are counted digit by digit.
		std::string Grouped(double value) {
			char digits[32];
			std::snprintf(digits, sizeof(digits), "%.0f", value < 0.0 ? 0.0 : value);

			std::string text = digits;
			for (size_t at = text.size(); at > 3;) {
				at -= 3;
				text.insert(at, ",");
			}
			return text;
		}

		std::string Share(float fraction) {
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%.1f%%", std::clamp(fraction, 0.0f, 1.0f) * 100.0f);
			return buffer;
		}

		// White until it matters. A row worth looking at should be findable
		// without reading every number on the way down.
		Colour TimeColour(float share) {
			if (share >= 0.5f) {
				return {255, 120, 110};
			}
			if (share >= 0.2f) {
				return {255, 205, 120};
			}
			return {205, 210, 220};
		}

		std::string RowLabel(const ProfilerRow &row, float frameMilliseconds) {
			std::string name(std::min(row.Depth, MAXIMUM_INDENT) * 2, ' ');
			name += std::string(row.Name);
			// A leaf and a row whose children are on another tab look the same
			// otherwise, and they mean different things.
			if (row.Collapsed) {
				name += " +";
			}

			float share = frameMilliseconds > 0.0f ? row.Milliseconds / frameMilliseconds : 0.0f;
			return PadRight(std::move(name), NAME_FIELD) + " " +
				   PadLeft(Milliseconds(row.Milliseconds), VALUE_FIELD) + " " +
				   PadLeft(Milliseconds(row.RecentMaxMilliseconds), RMAX_FIELD) + " " +
				   PadLeft(Share(share), SHARE_FIELD);
		}

		// Everything above and below the rows: the tab strip, the frame line, the
		// Main tab's category summary, the column header and the status line.
		// Needed before the image is sized, which is before anything is drawn.
		int ProfilerFixedHeight(const ProfilerView &view) {
			// The tab strip takes two, for the strip and the air under it.
			int height = LINE_HEIGHT * 2 + LINE_HEIGHT + LINE_HEIGHT; // tabs, frame, columns
			if (view.Tab == ProfilerTab::Main) {
				height += (int)ProfilerCategory::Count * ROW_HEIGHT + 4;
			}
			// The line the tab exists for: what the world holds.
			if (view.Tab == ProfilerTab::Counters) {
				height += LINE_HEIGHT;
			}
			return height + LINE_HEIGHT; // status
		}

		int ProfilerWidth() {
			return CHIP_WIDTH + CHIP_GAP + MeasureChars(ROW_CHARS) + TIMELINE_GAP + TIMELINE_WIDTH;
		}

		// Where a span of the frame lands in the timeline column.
		void DrawSpan(
			OverlayImage &image,
			int x,
			int y,
			int width,
			float startMilliseconds,
			float milliseconds,
			float frameMilliseconds,
			Colour colour,
			uint8_t alpha
		) {
			if (frameMilliseconds <= 0.0f || width <= 0) {
				return;
			}

			float scale = (float)width / frameMilliseconds;
			int left = x + (int)(std::max(startMilliseconds, 0.0f) * scale);
			// At least a pixel: a zone too short to see still happened, and a gap
			// where one should be reads as a zone that did not run.
			int span = std::max(1, (int)(milliseconds * scale));
			if (left >= x + width) {
				return;
			}
			span = std::min(span, x + width - left);

			image.Blend(left, y, span, ROW_HEIGHT - 4, colour.Red, colour.Green, colour.Blue, alpha);
		}

		void DrawTabStrip(OverlayImage &image, int x, int y, int width, ProfilerTab active) {
			int pen = x;
			for (size_t index = 0; index < (size_t)ProfilerTab::Count; index++) {
				auto tab = (ProfilerTab)index;
				std::string_view name = GetProfilerTabName(tab);
				int textWidth = DebugText::Measure(name, SCALE);

				if (tab == active) {
					// The strip is the only thing saying which tab is showing, so
					// the active one is a filled chip rather than a brighter word
					image.Blend(pen - 3, y - 3, textWidth + 6, LINE_HEIGHT, 90, 130, 190, 190);
					DebugText::Draw(image, pen, y, name, 255, 255, 255, SCALE);
				} else {
					DebugText::Draw(image, pen, y, name, 125, 128, 140, SCALE);
				}

				pen += textWidth + DebugText::ADVANCE * SCALE * 2;
			}

			// Nothing on screen says the arrows do anything, and a panel with a
			// hidden control has none.
			std::string_view hint = "<> TABS  QE DEPTH";
			int hintWidth = DebugText::Measure(hint, SCALE);
			if (pen + DebugText::ADVANCE * SCALE + hintWidth <= x + width) {
				DebugText::Draw(image, x + width - hintWidth, y, hint, 110, 115, 128, SCALE);
			}
		}

		// The Main tab's answer to "where did the frame go", before any of the
		// detail tabs are opened.
		void DrawCategorySummary(OverlayImage &image, int x, int y, int width, const ProfilerView &view) {
			int timelineX = x + width - TIMELINE_WIDTH;

			for (size_t index = 0; index < (size_t)ProfilerCategory::Count; index++) {
				float milliseconds = view.CategoryMilliseconds[index];
				float share = view.FrameMilliseconds > 0.0f ? milliseconds / view.FrameMilliseconds : 0.0f;
				Colour colour = CategoryColour((ProfilerCategory)index);

				static constexpr std::string_view NAMES[] = {"ENGINE", "RENDER", "PHYSICS", "LUAU"};
				// The RMAX column is left blank rather than filled: these are
				// per-category totals for this frame, and a recent worst for a
				// total is a different question from a recent worst for a zone.
				// Blank keeps SHARE under its own header.
				std::string text = PadRight(std::string(NAMES[index]), NAME_FIELD) + " " +
								   PadLeft(Milliseconds(milliseconds), VALUE_FIELD) + " " +
								   PadLeft("", RMAX_FIELD) + " " + PadLeft(Share(share), SHARE_FIELD);

				image.Blend(x, y, CHIP_WIDTH, ROW_HEIGHT - 3, colour.Red, colour.Green, colour.Blue, 255);
				DebugText::Draw(
					image, x + CHIP_WIDTH + CHIP_GAP, y, text, colour.Red, colour.Green, colour.Blue, SCALE
				);

				// Filled from the left rather than placed in the frame: this is a
				// total, and the parts it came from were spread across the frame.
				int barWidth = std::max(0, (int)(share * (float)TIMELINE_WIDTH));
				image.Blend(timelineX, y, TIMELINE_WIDTH, ROW_HEIGHT - 4, 255, 255, 255, 26);
				image.Blend(timelineX, y, barWidth, ROW_HEIGHT - 4, colour.Red, colour.Green, colour.Blue, 200);

				y += ROW_HEIGHT;
			}
		}

		// Counters share the zone rows' column widths, so the bars line up down
		// the panel whichever tab is open.
		constexpr size_t COUNTER_VALUE_FIELD = VALUE_FIELD + 1 + RMAX_FIELD + 1 + SHARE_FIELD;

		std::string CounterLabel(const ProfilerCounter &counter) {
			std::string name(counter.Name);
			// Summed over the frame rather than read once, which is a different
			// number and has to look like one.
			if (counter.Samples > 1) {
				name += " X" + std::to_string(counter.Samples);
			}

			std::string value = counter.IsTime ? Milliseconds((float)counter.Value) + " MS" : Grouped(counter.Value);
			return PadRight(std::move(name), NAME_FIELD) + " " + PadLeft(std::move(value), COUNTER_VALUE_FIELD);
		}

		void DrawCounters(OverlayImage &image, int x, int y, int width, const ProfilerView &view, int visibleRows) {
			int timelineX = x + width - TIMELINE_WIDTH;
			int drawn = std::min(visibleRows, (int)view.Counters.size() - view.Scroll);

			if (view.Counters.empty()) {
				DebugText::Draw(image, x + CHIP_WIDTH + CHIP_GAP, y, "COLLECTING", 150, 210, 255, SCALE);
				return;
			}

			for (int index = 0; index < drawn; index++) {
				const ProfilerCounter &counter = view.Counters[(size_t)(view.Scroll + index)];
				Colour colour = CategoryColour(counter.Category);

				if ((index & 1) != 0) {
					image.Blend(x, y - 1, width, ROW_HEIGHT, 255, 255, 255, 16);
				}
				image.Blend(x, y, CHIP_WIDTH, ROW_HEIGHT - 3, colour.Red, colour.Green, colour.Blue, 255);

				std::string text = CounterLabel(counter);
				DebugText::Draw(
					image,
					x + CHIP_WIDTH + CHIP_GAP,
					y,
					text.substr(0, NAME_FIELD),
					colour.Red,
					colour.Green,
					colour.Blue,
					SCALE
				);
				DebugText::Draw(
					image,
					x + CHIP_WIDTH + CHIP_GAP + MeasureChars(NAME_FIELD + 1),
					y,
					text.substr(NAME_FIELD + 1),
					225,
					228,
					235,
					SCALE
				);

				// Against the largest counter of its own kind, filled from the
				// left: a count has no place in the frame to be drawn at.
				int barWidth = std::max(0, (int)(std::clamp(counter.Share, 0.0f, 1.0f) * (float)TIMELINE_WIDTH));
				image.Blend(timelineX, y, TIMELINE_WIDTH, ROW_HEIGHT - 4, 255, 255, 255, 26);
				image.Blend(timelineX, y, barWidth, ROW_HEIGHT - 4, colour.Red, colour.Green, colour.Blue, 210);

				y += ROW_HEIGHT;
			}
		}

		// How many lines the open tab has, which is what scrolling is measured in.
		size_t ProfilerRowCount(const ProfilerView &view) {
			return view.Tab == ProfilerTab::Counters ? view.Counters.size() : view.Rows.size();
		}

		void DrawProfiler(OverlayImage &image, int x, int y, int width, const ProfilerView &view, int visibleRows) {
			int timelineX = x + width - TIMELINE_WIDTH;

			DrawTabStrip(image, x, y, width, view.Tab);
			y += LINE_HEIGHT * 2;

			// The frame total, because every share below is a share of it.
			std::string frameLine = "FRAME " + Milliseconds(view.FrameMilliseconds) + " MS";
			if (view.FrameMilliseconds > 0.0f) {
				frameLine += "  " + Rate(1000.0f / view.FrameMilliseconds) + " FPS";
			}
			// Beside FPS because it answers what FPS cannot: whether the frames
			// arriving at that rate are arriving evenly.
			frameLine += "  " + Milliseconds(view.FrameJitter) + " JIT";
			frameLine += "  " + std::to_string(ProfilerRowCount(view)) +
						 (view.Tab == ProfilerTab::Counters ? " COUNTERS" : " ZONES");
			// Only where it does something. On Main and Full the number would be
			// there to be adjusted and ignored.
			if (view.Tab != ProfilerTab::Main && view.Tab != ProfilerTab::Full &&
				view.Tab != ProfilerTab::Counters) {
				frameLine += "  DEPTH " + std::to_string(view.DepthLimit);
			}
			DebugText::Draw(image, x, y, frameLine, 255, 210, 120, SCALE);
			y += LINE_HEIGHT;

			if (view.Tab == ProfilerTab::Counters) {
				// The two the tab was asked for, up top and not to be scrolled
				// past: everything below is a breakdown of one of them.
				std::string totals = "OBJECTS " + Grouped((double)view.TotalObjects) + "   TRIANGLES " +
									 Grouped((double)view.TotalTriangles);
				DebugText::Draw(image, x, y, totals, 150, 210, 255, SCALE);
				y += LINE_HEIGHT;
			}

			if (view.Tab == ProfilerTab::Main) {
				DrawCategorySummary(image, x, y, width, view);
				y += (int)ProfilerCategory::Count * ROW_HEIGHT + 4;
			}

			// Column header, so the numbers are not left to be guessed at.
			bool counters = view.Tab == ProfilerTab::Counters;
			std::string header = counters ? PadRight("COUNTER", NAME_FIELD) + " " +
												PadLeft("VALUE", COUNTER_VALUE_FIELD)
										  : PadRight("ZONE", NAME_FIELD) + " " + PadLeft("MS", VALUE_FIELD) + " " +
												PadLeft("RMAX", RMAX_FIELD) + " " + PadLeft("SHARE", SHARE_FIELD);
			DebugText::Draw(image, x + CHIP_WIDTH + CHIP_GAP, y, header, 130, 135, 148, SCALE);
			DebugText::Draw(
				image, timelineX, y, counters ? "OF ITS GROUP" : "FRAME TIMELINE", 130, 135, 148, SCALE
			);
			y += LINE_HEIGHT;

			if (counters) {
				DrawCounters(image, x, y, width, view, visibleRows);
				return;
			}

			int rowsTop = y;
			int drawn = std::min(visibleRows, (int)view.Rows.size() - view.Scroll);

			// Quarters of the frame, behind the rows: a bar's position means
			// nothing without something to measure it against.
			if (drawn > 0) {
				for (int quarter = 1; quarter < 4; quarter++) {
					int line = timelineX + TIMELINE_WIDTH * quarter / 4;
					image.Blend(line, rowsTop, 1, drawn * ROW_HEIGHT, 255, 255, 255, 26);
				}
			}

			if (view.Rows.empty()) {
				DebugText::Draw(image, x + CHIP_WIDTH + CHIP_GAP, y, "COLLECTING", 150, 210, 255, SCALE);
			}

			for (int index = 0; index < drawn; index++) {
				const ProfilerRow &row = view.Rows[(size_t)(view.Scroll + index)];
				Colour colour = Shade(CategoryColour(row.Category), row.Depth);
				float share = view.FrameMilliseconds > 0.0f ? row.Milliseconds / view.FrameMilliseconds : 0.0f;

				// Every other row tinted. The rows are one pixel apart and a
				// column of numbers with nothing between them is read wrong.
				if ((index & 1) != 0) {
					image.Blend(x, y - 1, width, ROW_HEIGHT, 255, 255, 255, 16);
				}

				image.Blend(x, y, CHIP_WIDTH, ROW_HEIGHT - 3, colour.Red, colour.Green, colour.Blue, 255);

				std::string text = RowLabel(row, view.FrameMilliseconds);
				// Name in the category's colour, numbers in the heat colour: the
				// name says what it is, the number says whether to care.
				DebugText::Draw(
					image,
					x + CHIP_WIDTH + CHIP_GAP,
					y,
					text.substr(0, NAME_FIELD),
					colour.Red,
					colour.Green,
					colour.Blue,
					SCALE
				);
				Colour heat = TimeColour(share);
				DebugText::Draw(
					image,
					x + CHIP_WIDTH + CHIP_GAP + MeasureChars(NAME_FIELD + 1),
					y,
					text.substr(NAME_FIELD + 1),
					heat.Red,
					heat.Green,
					heat.Blue,
					SCALE
				);

				image.Blend(timelineX, y, TIMELINE_WIDTH, ROW_HEIGHT - 4, 255, 255, 255, 26);
				DrawSpan(
					image,
					timelineX,
					y,
					TIMELINE_WIDTH,
					row.StartMilliseconds,
					row.Milliseconds,
					view.FrameMilliseconds,
					colour,
					230
				);

				y += ROW_HEIGHT;
			}
		}
	} // namespace

	void DrawDebugPanels(
		OverlayImage &image,
		const FrameStatistics *statistics,
		ProfilerView *profiler,
		bool tracyConnected
	) {
		std::vector<Line> lines;

		if (statistics && statistics->HasSamples()) {
			lines.push_back({Column("FPS", statistics->Current()), 255, 255, 255});
			// Dimmer than the live number, which the eye should land on first
			lines.push_back(
				{Column("MIN", statistics->Minimum()) + "  " + Column("AVG", statistics->Average()) + "  " +
					 Column("MAX", statistics->Maximum()),
				 150,
				 210,
				 255}
			);
		}

		if (lines.empty() && !profiler) {
			image.Resize(0, 0);
			return;
		}

		int width = 0;
		for (const auto &line : lines) {
			width = std::max(width, DebugText::Measure(line.Text, SCALE));
		}

		int statisticsHeight = LINE_HEIGHT * (int)lines.size();
		int profilerHeight = 0;
		int visibleRows = 0;

		if (profiler) {
			width = std::max(width, ProfilerWidth());

			// One line of air between the counter and the panel below it. A rule
			// would read as another row: the font has no box drawing.
			if (!lines.empty()) {
				statisticsHeight += LINE_HEIGHT / 2;
			}

			int fixed = ProfilerFixedHeight(*profiler);
			int room = profiler->MaximumHeight - PADDING * 2 - statisticsHeight - fixed;
			// At least one row: a panel too short to show anything should still
			// show the tab strip and say so, not collapse to a stripe.
			visibleRows = std::max(1, room / ROW_HEIGHT);
			visibleRows = std::min(visibleRows, std::max<int>(1, (int)ProfilerRowCount(*profiler)));

			// Clamped here because this is the only place that knows how many
			// rows fit, and the key that moved it does not.
			int maximumScroll = std::max(0, (int)ProfilerRowCount(*profiler) - visibleRows);
			profiler->Scroll = std::clamp(profiler->Scroll, 0, maximumScroll);

			profilerHeight = fixed + visibleRows * ROW_HEIGHT;
		}

		width += PADDING * 2;
		int height = statisticsHeight + profilerHeight + PADDING * 2;
		if (!profiler) {
			// Trims the trailing line gap in a text-only panel
			height -= 4;
		}

		image.Resize(width, height);

		// Readable over a bright scene without hiding it. The profiler is denser
		// than the counter and forty rows of 3x5 glyphs over a lit scene is not
		// readable at the alpha two lines of large ones are.
		image.Blend(0, 0, width, height, 8, 8, 12, profiler ? 216 : 191);

		int y = PADDING;
		for (const auto &line : lines) {
			if (!line.Text.empty()) {
				DebugText::Draw(image, PADDING, y, line.Text, line.Red, line.Green, line.Blue, SCALE);
			}
			y += LINE_HEIGHT;
		}

		if (!profiler) {
			return;
		}
		if (!lines.empty()) {
			y += LINE_HEIGHT / 2;
		}

		int contentWidth = width - PADDING * 2;
		DrawProfiler(image, PADDING, y, contentWidth, *profiler, visibleRows);

		// Status, along the bottom: what is on screen out of what there is, what
		// was thrown away, and whether the deep tool is listening.
		std::string status;
		size_t rowCount = ProfilerRowCount(*profiler);
		if (rowCount > 0) {
			int first = profiler->Scroll + 1;
			int last = std::min((int)rowCount, profiler->Scroll + visibleRows);
			status = "ROWS " + std::to_string(first) + "/" + std::to_string(last) + " OF " +
					 std::to_string(rowCount);
			if ((int)rowCount > visibleRows) {
				status += " (UP DOWN)";
			}
		}
		if (profiler->Dropped > 0) {
			status += "  +" + std::to_string(profiler->Dropped) + " DROPPED";
		}
		status += tracyConnected ? "  TRACY CONNECTED" : "  TRACY WAITING";

		DebugText::Draw(image, PADDING, height - PADDING - LINE_HEIGHT + 4, status, 140, 145, 158, SCALE);
	}
} // namespace gargantuan
