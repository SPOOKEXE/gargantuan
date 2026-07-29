#pragma once

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Vector2.hpp"

#include <deque>
#include <memory>
#include <string_view>

namespace gargantuan {
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
		double Span() const;

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
			EditableImage &image, Vector2 position, std::string_view text, Color3 colour, float transparency, int scale
		);
	} // namespace DebugText

	// Resizes `image` to fit and returns the size it settled on
	Vector2 DrawStatisticsPanel(EditableImage &image, const FrameStatistics &statistics);

	// The rectangle is in the panel's own coordinates; whoever placed the panel
	// is the only one who can turn a mouse position into a hit
	struct ProfilerPanelLayout {
		Vector2 Size = Vector2(0, 0);
		Vector2 ButtonPosition = Vector2(0, 0);
		Vector2 ButtonSize = Vector2(0, 0);
	};

	// `status` is a line along the bottom: what an export wrote, or that it is
	// still gathering its first second
	ProfilerPanelLayout DrawProfilerPanel(
		EditableImage &image, const Profiler::Snapshot &snapshot, std::string_view status
	);
} // namespace gargantuan
