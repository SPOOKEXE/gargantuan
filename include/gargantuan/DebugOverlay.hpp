#pragma once

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Vector2.hpp"

#include <deque>
#include <memory>
#include <string_view>

namespace gargantuan {
	// How the frame rate has behaved over the last little while, which is a
	// different question from what it is right now. A number that only says
	// "143" tells you nothing about the frame that took a fifth of a second
	// somewhere in the last few seconds, and that frame is the one worth
	// knowing about.
	class FrameStatistics {
	  public:
		// Long enough that a stutter stays on screen after it is over, short
		// enough that the numbers still describe where the camera is now rather
		// than where it was
		static constexpr double WINDOW_SECONDS = 20.0;

		// `now` is real elapsed seconds, not game time, so a place that slows
		// its own clock down does not report a frame rate it is not achieving
		void Record(double now, float deltaTime);

		bool HasSamples() const;
		// Frames a second, from the most recent frame alone
		float Current() const;
		// The slowest frame in the window, which is the one that was visible as
		// a stutter. Taken from the longest single frame rather than an average
		// of the slow ones, because one frame is all it takes to be seen.
		float Minimum() const;
		// Frames divided by the time they took, not the mean of their rates.
		// Averaging rates weights a fast frame the same as a slow one and
		// flatters a run that hitched.
		float Average() const;
		float Maximum() const;
		// How much of the window has actually been filled, so a readout can say
		// it is still settling rather than quoting a minimum from three frames
		double Span() const;

		void Clear();

	  private:
		struct Sample {
			double Time = 0.0;
			float Delta = 0.0f;
		};

		std::deque<Sample> Samples;
	};

	// A 3x5 pixel font, enough to label a debug readout. Not the DrawText the
	// engine still owes: no kerning, no lowercase, no measuring beyond counting
	// characters. It exists because a number on screen needs a word next to it
	// saying which number it is, and waiting for real text meant no readout at
	// all.
	namespace DebugText {
		// A glyph cell, before scaling
		static constexpr int GLYPH_WIDTH = 3;
		static constexpr int GLYPH_HEIGHT = 5;
		// One blank column between characters
		static constexpr int ADVANCE = GLYPH_WIDTH + 1;

		// Width in pixels that `text` will occupy at `scale`
		int Measure(std::string_view text, int scale);

		// Draws at `position`, top-left, one lit cell per set bit scaled up.
		// Anything the font does not have is skipped rather than substituted,
		// so a missing glyph leaves a gap instead of a wrong letter.
		void Draw(
			EditableImage &image, Vector2 position, std::string_view text, Color3 colour, float transparency, int scale
		);
	} // namespace DebugText

	// Paints the frame rate readout into `image`, resizing it to fit. Returns
	// the size it settled on.
	Vector2 DrawStatisticsPanel(EditableImage &image, const FrameStatistics &statistics);

	// What the profiler panel came out as, and where the one thing on it that
	// can be clicked ended up. The rectangle is in the panel's own coordinates;
	// whoever placed the panel knows where that is on the window and is the
	// only one who can turn a mouse position into a hit.
	struct ProfilerPanelLayout {
		Vector2 Size = Vector2(0, 0);
		Vector2 ButtonPosition = Vector2(0, 0);
		Vector2 ButtonSize = Vector2(0, 0);
	};

	// Paints the flame chart. `status` is a line along the bottom for whatever
	// the panel wants to say back -- what an export wrote, or that it is still
	// gathering its first second.
	ProfilerPanelLayout DrawProfilerPanel(
		EditableImage &image, const Profiler::Snapshot &snapshot, std::string_view status
	);
} // namespace gargantuan
