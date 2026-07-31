#include "gargantuan/render/ImageDecoder.hpp"
#include "gargantuan/Paths.hpp"

#include <SDL3/SDL.h>
#include <filesystem>

// Sole stb_image implementation unit.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace gargantuan::ImageDecoder {
	Image Decode(const void *bytes, size_t encodedByteCount) {
		Image image;

		if (!bytes || encodedByteCount == 0) {
			image.ErrorMessage = "No image data";
			return image;
		}

		int width = 0, height = 0, channels = 0;
		stbi_uc *pixels = stbi_load_from_memory(
			static_cast<const stbi_uc *>(bytes), (int)encodedByteCount, &width, &height, &channels, 4
		);

		if (!pixels) {
			const char *reason = stbi_failure_reason();
			image.ErrorMessage = reason ? reason : "Could not decode the image";
			return image;
		}

		image.Width = width;
		image.Height = height;
		image.RGBA8Pixels.assign(pixels, pixels + (size_t)width * height * 4);
		image.WasDecoded = true;
		stbi_image_free(pixels);
		return image;
	}

	// PNG filter 2 is "up": each byte stored as its difference from the byte above
	// it. stb's default of -1 encodes every row five times, once per filter, and
	// keeps whichever came out smallest.
	//
	// Worth it for arbitrary images, not for ours. Measured on a saved 1920x1048
	// frame: trying all five took 81 ms for 518 KB, forcing up took 51 ms for
	// 521 KB. Six tenths of a percent larger for a third less time, because a
	// rendered frame is mostly vertical gradients and up is the filter the search
	// was going to pick anyway. A 384x384 camera feed goes 6.0 ms to 4.0 ms at the
	// same 47 KB.
	//
	// Compression level is left alone: sweeping it from 8 down to 0 moved the same
	// frame by under 10%, so there is nothing there to trade away quality for.
	constexpr int PNG_FILTER_UP = 2;

	bool WritePng(const std::string &path, int width, int height, const uint8_t *rgba) {
		if (!rgba || width <= 0 || height <= 0) {
			return false;
		}

		stbi_write_force_png_filter = PNG_FILTER_UP;
		return stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
	}

	bool WriteJpg(const std::string &path, int width, int height, const uint8_t *rgba) {
		if (!rgba || width <= 0 || height <= 0) {
			return false;
		}

		return stbi_write_jpg(path.c_str(), width, height, 4, rgba, JPEG_QUALITY) != 0;
	}

	bool WriteBmp(const std::string &path, int width, int height, const uint8_t *rgba) {
		if (!rgba || width <= 0 || height <= 0) {
			return false;
		}

		return stbi_write_bmp(path.c_str(), width, height, 4, rgba) != 0;
	}

	Image DecodeFile(const std::string &path) {
		Image image;

		std::filesystem::path resolved{path};
		if (resolved.is_relative()) {
			resolved = Paths::GetExecutableDirectory() / resolved;
		}

		size_t size = 0;
		void *bytes = SDL_LoadFile(resolved.string().c_str(), &size);
		if (!bytes) {
			image.ErrorMessage = "Could not read " + resolved.string();
			return image;
		}

		image = Decode(bytes, size);
		SDL_free(bytes);
		return image;
	}
} // namespace gargantuan::ImageDecoder
