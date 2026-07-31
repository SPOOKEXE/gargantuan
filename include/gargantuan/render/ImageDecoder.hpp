#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gargantuan::ImageDecoder {
	struct Image {
		std::vector<uint8_t> RGBA8Pixels;
		int Width = 0;
		int Height = 0;
		bool WasDecoded = false;
		std::string ErrorMessage;
	};

	Image Decode(const void *bytes, size_t encodedByteCount);

	Image DecodeFile(const std::string &path);

	bool WritePng(const std::string &path, int width, int height, const uint8_t *rgba);
	constexpr int JPEG_QUALITY = 90;
	bool WriteJpg(const std::string &path, int width, int height, const uint8_t *rgba);
	bool WriteBmp(const std::string &path, int width, int height, const uint8_t *rgba);
}
