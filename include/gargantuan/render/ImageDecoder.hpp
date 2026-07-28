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
} // namespace gargantuan::ImageDecoder
