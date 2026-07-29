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

	// Decodes PNG, JPEG, BMP, TGA, GIF, or PSD into RGBA8 pixels.
	Image Decode(const void *bytes, size_t size);

	// Reads a file and decodes it. Relative paths resolve against the
	// executable's directory, which is where the engine's assets live.
	Image DecodeFile(const std::string &path);

	bool WritePng(const std::string &path, int width, int height, const uint8_t *rgba);
	// JPEG drops alpha; PNG and BMP preserve it.
	constexpr int JPEG_QUALITY = 90;
	bool WriteJpg(const std::string &path, int width, int height, const uint8_t *rgba);
	bool WriteBmp(const std::string &path, int width, int height, const uint8_t *rgba);
} // namespace gargantuan::ImageDecoder
