#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gargantuan::ImageDecoder {
	struct Image {
		// RGBA8, row-major, top-left origin
		std::vector<uint8_t> Pixels;
		int Width = 0;
		int Height = 0;
		bool Success = false;
		std::string Error;
	};

	// Decodes an image already in memory. PNG, JPEG, BMP, TGA, GIF and PSD are
	// all understood; the result is always converted to RGBA8.
	Image Decode(const void *bytes, size_t size);

	// Reads a file and decodes it. Relative paths resolve against the
	// executable's directory, which is where the engine's assets live.
	Image DecodeFile(const std::string &path);

	// Write RGBA8 pixels out. PNG and BMP both carry the alpha through, BMP as
	// a 32-bit bitmap. JPEG has nowhere to put it, so a half-transparent pixel
	// lands as its full colour rather than blended with anything.
	bool WritePng(const std::string &path, int width, int height, const uint8_t *rgba);
	// Quality is stb's 1..100. High enough that the ringing around hard edges
	// is not the first thing anyone notices in a screenshot.
	constexpr int JPEG_QUALITY = 90;
	bool WriteJpg(const std::string &path, int width, int height, const uint8_t *rgba);
	bool WriteBmp(const std::string &path, int width, int height, const uint8_t *rgba);
} // namespace gargantuan::ImageDecoder
