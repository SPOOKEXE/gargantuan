#pragma once

#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

namespace gargantuan {
	// Somewhere to put pixels, uploaded once a frame. Deliberately not
	// EditableImage: the overlay wants a buffer, not a Luau-facing image class,
	// and nothing here should be reachable from a script.
	class OverlayImage {
	  public:
		void Resize(int width, int height);

		// Source-over. The panel background goes down first and the text has to
		// land on top of it rather than replace it.
		void Blend(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);

		int GetWidth() const {
			return Width;
		}
		int GetHeight() const {
			return Height;
		}
		const uint8_t *GetPixels() const {
			return Pixels.data();
		}
		bool IsEmpty() const {
			return Width <= 0 || Height <= 0;
		}

	  private:
		int Width = 0;
		int Height = 0;
		// RGBA8, row major, no padding between rows -- what the GPU transfer
		// buffer wants, so the upload is one memcpy.
		std::vector<uint8_t> Pixels;
	};

	// How the frame rate has behaved lately, which is a different question from
	// what it is right now: "143" says nothing about the frame that took a
	// fifth of a second, and that is the one worth knowing about.
	class FrameStatistics {
	  public:
		static constexpr double WINDOW_SECONDS = 20.0;

		// `now` is real elapsed seconds, not game time
		void Record(double now, float deltaTime);

		bool HasSamples() const;
		float Current() const;
		// From the longest single frame, not an average of the slow ones: one
		// frame is all it takes to be seen
		float Minimum() const;
		// Frames divided by the time they took. The mean of the rates would
		// weight a fast frame the same as a slow one.
		float Average() const;
		float Maximum() const;

		void Clear();

	  private:
		struct Sample {
			double Time = 0.0;
			float Delta = 0.0f;
		};

		std::deque<Sample> Samples;
	};

	// A 3x5 pixel font. Not the DrawText the engine still owes -- no kerning,
	// no lowercase -- but a number on screen needs a word saying which number
	// it is.
	namespace DebugText {
		static constexpr int GLYPH_WIDTH = 3;
		static constexpr int GLYPH_HEIGHT = 5;
		static constexpr int ADVANCE = GLYPH_WIDTH + 1;

		int Measure(std::string_view text, int scale);

		// Anything the font lacks is skipped, so a missing glyph is a gap
		// rather than a wrong letter
		void Draw(
			OverlayImage &image,
			int x,
			int y,
			std::string_view text,
			uint8_t red,
			uint8_t green,
			uint8_t blue,
			int scale
		);
	} // namespace DebugText

	// One row of the F5 panel. Phase is printed only when it changes, so the
	// systems under it read as a group.
	struct SystemTiming {
		std::string_view Phase;
		std::string_view Name;
		float Milliseconds = 0.0f;
	};

	// Sizes `image` to whatever is turned on and draws the panels stacked.
	// Either pointer may be null; both null leaves the image empty and the
	// overlay pass draws nothing.
	void DrawDebugPanels(
		OverlayImage &image,
		const FrameStatistics *statistics,
		const std::vector<SystemTiming> *systems,
		bool tracyConnected
	);
} // namespace gargantuan
