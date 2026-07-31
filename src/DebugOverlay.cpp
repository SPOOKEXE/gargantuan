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

			constexpr std::array<Glyph, 38> GLYPHS = {{
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

		// Name left, time right, so the eye runs down the numbers rather than
		// hunting for them at the end of names of different lengths
		std::string SystemRow(std::string_view name, float milliseconds) {
			constexpr size_t NAME_FIELD = 20;

			std::string text(name.substr(0, NAME_FIELD));
			text.append(NAME_FIELD - text.size(), ' ');

			std::string value = Milliseconds(milliseconds);
			constexpr size_t VALUE_FIELD = 7;
			text.append(VALUE_FIELD - std::min(value.size(), VALUE_FIELD - 1), ' ');
			return text + value;
		}
	} // namespace

	void DrawDebugPanels(
		OverlayImage &image,
		const FrameStatistics *statistics,
		const std::vector<SystemTiming> *systems,
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

		if (systems) {
			// A blank line rather than a rule: the font has no box drawing and
			// a row of dashes reads as another system
			if (!lines.empty()) {
				lines.push_back({"", 0, 0, 0});
			}

			std::string_view phase;
			for (const auto &timing : *systems) {
				if (timing.Phase != phase) {
					phase = timing.Phase;
					lines.push_back({std::string(phase), 255, 210, 120});
				}
				lines.push_back({"  " + SystemRow(timing.Name, timing.Milliseconds), 200, 200, 210});
			}

			// Tracy is the deep tool; this panel only says whether it is
			// listening, because on-demand collection starts when it attaches
			// and there is otherwise no way to tell from in here.
			lines.push_back({"", 0, 0, 0});
			if (tracyConnected) {
				lines.push_back({"TRACY CONNECTED", 140, 230, 150});
			} else {
				lines.push_back({"TRACY WAITING", 190, 190, 190});
			}
		}

		if (lines.empty()) {
			image.Resize(0, 0);
			return;
		}

		int width = 0;
		for (const auto &line : lines) {
			width = std::max(width, DebugText::Measure(line.Text, SCALE));
		}
		width += PADDING * 2;
		int height = LINE_HEIGHT * (int)lines.size() + PADDING * 2 - 4;

		image.Resize(width, height);

		// Readable over a bright scene without hiding it
		image.Blend(0, 0, width, height, 8, 8, 12, 191);

		int y = PADDING;
		for (const auto &line : lines) {
			if (!line.Text.empty()) {
				DebugText::Draw(image, PADDING, y, line.Text, line.Red, line.Green, line.Blue, SCALE);
			}
			y += LINE_HEIGHT;
		}
	}
} // namespace gargantuan
