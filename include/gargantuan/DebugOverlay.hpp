#pragma once

#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

namespace gargantuan {
	class OverlayImage {
	  public:
		void Resize(int width, int height);

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
		std::vector<uint8_t> Pixels;
	};

	class FrameStatistics {
	  public:
		static constexpr double WINDOW_SECONDS = 20.0;

		void Record(double now, float deltaTime);

		bool HasSamples() const;
		float Current() const;
		float Minimum() const;
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

	namespace DebugText {
		static constexpr int GLYPH_WIDTH = 3;
		static constexpr int GLYPH_HEIGHT = 5;
		static constexpr int ADVANCE = GLYPH_WIDTH + 1;

		int Measure(std::string_view text, int scale);

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
	}

	enum class ProfilerTab : uint8_t {
		Main,
		Render,
		Physics,
		Luau,
		Full,
		Counters,

		Count,
	};

	std::string_view GetProfilerTabName(ProfilerTab tab);

	enum class ProfilerCategory : uint8_t {
		Engine,
		Render,
		Physics,
		Luau,

		Count,
	};

	struct ProfilerRow {
		std::string_view Name;
		uint32_t Depth = 0;
		float StartMilliseconds = 0.0f;
		float Milliseconds = 0.0f;
		float RecentMaxMilliseconds = 0.0f;
		ProfilerCategory Category = ProfilerCategory::Engine;
		bool Collapsed = false;
	};

	struct ProfilerCounter {
		std::string_view Name;
		double Value = 0.0;
		uint32_t Samples = 1;
		bool IsTime = false;
		ProfilerCategory Category = ProfilerCategory::Engine;
		float Share = 0.0f;
	};

	struct ProfilerView {
		ProfilerTab Tab = ProfilerTab::Render;

		std::vector<ProfilerRow> Rows;

		std::vector<ProfilerCounter> Counters;
		uint64_t TotalObjects = 0;
		uint64_t TotalTriangles = 0;

		float FrameMilliseconds = 0.0f;
		// Mean frame-to-frame change over the recent window, shown beside FPS.
		float FrameJitter = 0.0f;

		float CategoryMilliseconds[(size_t)ProfilerCategory::Count] = {};

		size_t Dropped = 0;

		int Scroll = 0;

		static constexpr uint32_t DEFAULT_DEPTH_LIMIT = 2;
		static constexpr uint32_t MAXIMUM_DEPTH_LIMIT = 12;
		uint32_t DepthLimit = DEFAULT_DEPTH_LIMIT;

		static constexpr int SCROLL_STEP = 10;

		int MaximumHeight = 512;
	};

	void DrawDebugPanels(
		OverlayImage &image,
		const FrameStatistics *statistics,
		ProfilerView *profiler,
		bool tracyConnected
	);
}
