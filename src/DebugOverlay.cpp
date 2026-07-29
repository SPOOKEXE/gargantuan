#include "gargantuan/DebugOverlay.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace gargantuan {
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

	double FrameStatistics::Span() const {
		if (Samples.size() < 2) {
			return 0.0;
		}
		return Samples.back().Time - Samples.front().Time;
	}

	namespace DebugText {
		namespace {
			// Five rows of three bits, most significant leftmost. Written out
			// because at this size every glyph is a judgement call.
			struct Glyph {
				char Character;
				std::array<uint8_t, GLYPH_HEIGHT> Rows;
			};

			constexpr std::array<Glyph, 37> GLYPHS = {{
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
			EditableImage &image, Vector2 position, std::string_view text, Color3 colour, float transparency, int scale
		) {
			scale = std::max(scale, 1);
			float pen = position.GetX();

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

							image.DrawRectangle(
								Vector2(pen + (float)(column * scale), position.GetY() + (float)(row * scale)),
								Vector2((float)scale, (float)scale),
								colour,
								transparency,
								Enums::ImageCombineType::BlendSourceOver
							);
						}
					}
				}

				pen += (float)(ADVANCE * scale);
			}
		}
	} // namespace DebugText

	namespace {
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
	} // namespace

	Vector2 DrawStatisticsPanel(EditableImage &image, const FrameStatistics &statistics) {
		constexpr int SCALE = 2;
		constexpr int PADDING = 6;
		constexpr int LINE_HEIGHT = DebugText::GLYPH_HEIGHT * SCALE + 4;

		std::string top = Column("FPS", statistics.Current());
		std::string bottom = Column("MIN", statistics.Minimum()) + "  " + Column("AVG", statistics.Average()) +
			"  " + Column("MAX", statistics.Maximum());

		int width = std::max(DebugText::Measure(top, SCALE), DebugText::Measure(bottom, SCALE)) + PADDING * 2;
		int height = LINE_HEIGHT * 2 + PADDING * 2 - 4;

		Vector2 size((float)width, (float)height);
		if (image.GetWidth() != width || image.GetHeight() != height) {
			image.Resize(size);
		}

		// Readable over a bright scene without hiding it
		image.DrawRectangle(
			Vector2(0, 0), size, Color3::fromRGB(8, 8, 12), 0.25f, Enums::ImageCombineType::Overwrite
		);

		DebugText::Draw(
			image,
			Vector2((float)PADDING, (float)PADDING),
			top,
			Color3::fromRGB(255, 255, 255),
			0.0f,
			SCALE
		);
		// Dimmer than the live number, which the eye should land on first
		DebugText::Draw(
			image,
			Vector2((float)PADDING, (float)(PADDING + LINE_HEIGHT)),
			bottom,
			Color3::fromRGB(150, 210, 255),
			0.0f,
			SCALE
		);

		return size;
	}
} // namespace gargantuan
