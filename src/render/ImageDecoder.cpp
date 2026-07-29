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
	Image Decode(const void *bytes, size_t size) {
		Image image;

		if (!bytes || size == 0) {
			image.Error = "No image data";
			return image;
		}

		int width = 0, height = 0, channels = 0;
		stbi_uc *pixels = stbi_load_from_memory(
			static_cast<const stbi_uc *>(bytes), (int)size, &width, &height, &channels, 4
		);

		if (!pixels) {
			const char *reason = stbi_failure_reason();
			image.Error = reason ? reason : "Could not decode the image";
			return image;
		}

		image.Width = width;
		image.Height = height;
		image.Pixels.assign(pixels, pixels + (size_t)width * height * 4);
		image.Success = true;
		stbi_image_free(pixels);
		return image;
	}

	bool WritePng(const std::string &path, int width, int height, const uint8_t *rgba) {
		if (!rgba || width <= 0 || height <= 0) {
			return false;
		}

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
			image.Error = "Could not read " + resolved.string();
			return image;
		}

		image = Decode(bytes, size);
		SDL_free(bytes);
		return image;
	}
} // namespace gargantuan::ImageDecoder
