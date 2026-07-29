#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/Paths.hpp"
#include "gargantuan/render/ImageDecoder.hpp"
#include "gargantuan/scripting/LuauBuffer.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <glm/glm.hpp>
#include <lualib.h>

namespace gargantuan {
	const EditableImage::ClassDefinition EditableImage::DEFINITION = {
		.Name = "EditableImage",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<EditableImage>(),
		.Properties =
			{
				{
					"SaveError",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<EditableImage>()->GetSaveError());
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(std::string),
					},
				},
				{
					"LoadError",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<EditableImage>()->GetLoadError());
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(std::string),
					},
				},
				{
					"Size",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<Vector2>::Push(L, instance->Cast<EditableImage>()->GetSize());
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(Vector2),
					},
				},
			},
		.Methods = {
			{"Resize", Method::Wrap<&EditableImage::Resize>()},
			{"Crop", Method::Wrap<&EditableImage::Crop>()},
			{"DrawRectangle", Method::Wrap<&EditableImage::DrawRectangle>()},
			{"DrawImage", Method::Wrap<&EditableImage::DrawImage>()},
			{"DrawCircle", Method::Wrap<&EditableImage::DrawCircle>()},
			{"DrawLine", Method::Wrap<&EditableImage::DrawLine>()},
			{"Load", Method::Wrap<&EditableImage::Load>()},
			{"Save",
			 {&EditableImage::LSave,
			  []() -> std::string { return "(self, path: string, format: Enum.SaveFormat?): boolean"; }}},
			{"ReadPixelsBuffer",
			 {&EditableImage::LReadPixelsBuffer,
			  []() -> std::string { return "(self, position: Vector2, size: Vector2): buffer"; }}},
			{"WritePixelsBuffer",
			 {&EditableImage::LWritePixelsBuffer,
			  []() -> std::string { return "(self, position: Vector2, size: Vector2, pixels: buffer): ()"; }}},
		}
	};

	uint64_t EditableImage::GetRevision() const {
		return Revision;
	}

	int EditableImage::GetWidth() const {
		return Width;
	}

	int EditableImage::GetHeight() const {
		return Height;
	}

	Vector2 EditableImage::GetSize() const {
		return Vector2((float)Width, (float)Height);
	}

	void EditableImage::Resize(Vector2 size) {
		Revision++;
		int width = glm::clamp((int)glm::round(size.GetX()), 0, MAXIMUM_DIMENSION);
		int height = glm::clamp((int)glm::round(size.GetY()), 0, MAXIMUM_DIMENSION);

		Width = width;
		Height = height;
		Pixels.assign((size_t)width * height * CHANNELS, 0);
	}

	void EditableImage::SetPixels(int width, int height, const uint8_t *rgba) {
		Revision++;
		Width = glm::max(width, 0);
		Height = glm::max(height, 0);
		Pixels.assign((size_t)Width * Height * CHANNELS, 0);

		if (rgba != nullptr && !Pixels.empty()) {
			std::memcpy(Pixels.data(), rgba, Pixels.size());
		}
	}

	bool EditableImage::ClampRegion(
		Vector2 position, Vector2 size, int &outX, int &outY, int &outWidth, int &outHeight
	) const {
		int x = (int)glm::round(position.GetX());
		int y = (int)glm::round(position.GetY());
		int width = (int)glm::round(size.GetX());
		int height = (int)glm::round(size.GetY());

		// Trim anything hanging off the top or left, keeping the rectangle's
		// far edge where it was
		if (x < 0) {
			width += x;
			x = 0;
		}
		if (y < 0) {
			height += y;
			y = 0;
		}

		width = glm::min(width, Width - x);
		height = glm::min(height, Height - y);

		outX = x;
		outY = y;
		outWidth = glm::max(width, 0);
		outHeight = glm::max(height, 0);

		return outWidth > 0 && outHeight > 0;
	}

	void EditableImage::Crop(Vector2 minimum, Vector2 maximum) {
		Revision++;
		int x = 0, y = 0, width = 0, height = 0;
		Vector2 size(maximum.GetX() - minimum.GetX(), maximum.GetY() - minimum.GetY());

		if (!ClampRegion(minimum, size, x, y, width, height)) {
			Width = 0;
			Height = 0;
			Pixels.clear();
			return;
		}

		std::vector<uint8_t> cropped((size_t)width * height * CHANNELS, 0);
		for (int row = 0; row < height; row++) {
			const uint8_t *source = Pixels.data() + ((size_t)(y + row) * Width + x) * CHANNELS;
			uint8_t *destination = cropped.data() + (size_t)row * width * CHANNELS;
			std::memcpy(destination, source, (size_t)width * CHANNELS);
		}

		Width = width;
		Height = height;
		Pixels = std::move(cropped);
	}

	namespace {
		// Integer division truncates, and every blend below divides by 255, so
		// plain `/ 255` loses up to a whole level and always downwards. One
		// draw is invisible; drawing repeatedly onto the same pixel walks it
		// darker every time. Rounding to nearest makes the error unbiased, so
		// it stops accumulating in one direction.
		//
		// Channels are kept as bytes rather than floats: rounding to nearest
		// already bounds the error at half a level per operation with no
		// drift, and floats would cost four times the memory for an image that
		// has to be quantised on save anyway.
		inline int DivideBy255(int value) {
			return (value + 127) / 255;
		}

		// The same, for a denominator that is not 255
		inline int DivideRounded(int value, int divisor) {
			return divisor <= 0 ? 0 : (value + divisor / 2) / divisor;
		}
	} // namespace

	void EditableImage::CombinePixel(
		int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, Enums::ImageCombineType combine, float coverage
	) {
		if (x < 0 || y < 0 || x >= Width || y >= Height || coverage <= 0.0f) {
			return;
		}

		// Coverage is how much of the pixel the shape touches, and is separate
		// from the source's own alpha. Mixing the two would make Overwrite
		// blend a half-transparent pixel instead of replacing it.
		int cover = (int)glm::round(glm::clamp(coverage, 0.0f, 1.0f) * 255.0f);
		uint8_t *pixel = Pixels.data() + ((size_t)y * Width + x) * CHANNELS;

		auto easeIn = [&](int red, int green, int blue, int alpha) {
			if (cover >= 255) {
				pixel[0] = (uint8_t)red;
				pixel[1] = (uint8_t)green;
				pixel[2] = (uint8_t)blue;
				pixel[3] = (uint8_t)alpha;
				return;
			}

			int inverse = 255 - cover;
			pixel[0] = (uint8_t)DivideBy255(red * cover + pixel[0] * inverse);
			pixel[1] = (uint8_t)DivideBy255(green * cover + pixel[1] * inverse);
			pixel[2] = (uint8_t)DivideBy255(blue * cover + pixel[2] * inverse);
			pixel[3] = (uint8_t)DivideBy255(alpha * cover + pixel[3] * inverse);
		};

		switch (combine) {
		case Enums::ImageCombineType::Overwrite:
			// Replaces outright; only a partly covered edge pixel eases in
			easeIn(r, g, b, a);
			return;

		case Enums::ImageCombineType::Add: {
			int alpha = DivideBy255(a * cover);
			easeIn(
				glm::min(255, pixel[0] + DivideBy255(r * alpha)),
				glm::min(255, pixel[1] + DivideBy255(g * alpha)),
				glm::min(255, pixel[2] + DivideBy255(b * alpha)),
				glm::min(255, pixel[3] + alpha)
			);
			return;
		}

		case Enums::ImageCombineType::Multiply: {
			int alpha = DivideBy255(a * cover);
			int inverse = 255 - alpha;
			pixel[0] = (uint8_t)DivideBy255(DivideBy255(pixel[0] * r) * alpha + pixel[0] * inverse);
			pixel[1] = (uint8_t)DivideBy255(DivideBy255(pixel[1] * g) * alpha + pixel[1] * inverse);
			pixel[2] = (uint8_t)DivideBy255(DivideBy255(pixel[2] * b) * alpha + pixel[2] * inverse);
			return;
		}

		case Enums::ImageCombineType::BlendSourceOver:
		default:
			break;
		}

		// Source-over, where coverage simply weakens the source
		int alpha = DivideBy255(a * cover);
		if (alpha <= 0) {
			return;
		}

		if (alpha >= 255) {
			pixel[0] = r;
			pixel[1] = g;
			pixel[2] = b;
			pixel[3] = 255;
			return;
		}

		// Proper straight-alpha compositing, which has to weight the
		// destination by its own alpha. Ignoring that is what gives translucent
		// draws over transparent pixels a dark halo.
		int inverse = 255 - alpha;
		int destinationAlpha = pixel[3];
		int outputAlpha = alpha + DivideBy255(destinationAlpha * inverse);

		if (outputAlpha <= 0) {
			pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
			return;
		}

		auto composite = [&](int source, int destination) {
			int numerator = source * alpha * 255 + destination * destinationAlpha * inverse;
			return (uint8_t)glm::clamp(DivideRounded(numerator, outputAlpha * 255), 0, 255);
		};

		pixel[0] = composite(r, pixel[0]);
		pixel[1] = composite(g, pixel[1]);
		pixel[2] = composite(b, pixel[2]);
		pixel[3] = (uint8_t)glm::min(255, outputAlpha);
	}

	namespace {
		// How much of a pixel a disc of `radius` centred at (cx, cy) covers.
		// Sampled on a 4x4 grid, which is enough to look smooth and costs
		// little; an exact area would need the circle-square intersection.
		float DiscCoverage(int x, int y, float cx, float cy, float radius) {
			float dx = (float)x - cx;
			float dy = (float)y - cy;
			float distance = glm::sqrt(dx * dx + dy * dy);

			// Well inside or well outside needs no sampling at all
			if (distance <= radius - 0.75f) {
				return 1.0f;
			}
			if (distance >= radius + 0.75f) {
				return 0.0f;
			}

			int inside = 0;
			for (int sy = 0; sy < 4; sy++) {
				for (int sx = 0; sx < 4; sx++) {
					float px = (float)x - 0.375f + sx * 0.25f;
					float py = (float)y - 0.375f + sy * 0.25f;
					float ox = px - cx;
					float oy = py - cy;
					if (ox * ox + oy * oy <= radius * radius) {
						inside++;
					}
				}
			}
			return inside / 16.0f;
		}

		// Same idea for a thick line segment, using distance to the segment
		float SegmentCoverage(int x, int y, glm::vec2 from, glm::vec2 to, float radius) {
			auto distanceTo = [&](float px, float py) {
				glm::vec2 point{px, py};
				glm::vec2 line = to - from;
				float lengthSquared = glm::dot(line, line);
				float t = lengthSquared > 0.0f ? glm::clamp(glm::dot(point - from, line) / lengthSquared, 0.0f, 1.0f)
											   : 0.0f;
				return glm::length(point - (from + line * t));
			};

			float centre = distanceTo((float)x, (float)y);
			if (centre <= radius - 0.75f) {
				return 1.0f;
			}
			if (centre >= radius + 0.75f) {
				return 0.0f;
			}

			int inside = 0;
			for (int sy = 0; sy < 4; sy++) {
				for (int sx = 0; sx < 4; sx++) {
					if (distanceTo((float)x - 0.375f + sx * 0.25f, (float)y - 0.375f + sy * 0.25f) <= radius) {
						inside++;
					}
				}
			}
			return inside / 16.0f;
		}
	} // namespace

	void EditableImage::DrawRectangle(
		Vector2 position, Vector2 size, Color3 color, float transparency, Enums::ImageCombineType combine
	) {
		Revision++;
		int x = 0, y = 0, width = 0, height = 0;
		if (!ClampRegion(position, size, x, y, width, height)) {
			return;
		}

		uint8_t red = (uint8_t)glm::round(glm::clamp(color.R, 0.0f, 1.0f) * 255.0f);
		uint8_t green = (uint8_t)glm::round(glm::clamp(color.G, 0.0f, 1.0f) * 255.0f);
		uint8_t blue = (uint8_t)glm::round(glm::clamp(color.B, 0.0f, 1.0f) * 255.0f);
		uint8_t alpha = (uint8_t)glm::round((1.0f - glm::clamp(transparency, 0.0f, 1.0f)) * 255.0f);

		// A rectangle lands on pixel boundaries, so there is nothing to
		// antialias; it goes straight through the combine
		for (int row = 0; row < height; row++) {
			for (int column = 0; column < width; column++) {
				CombinePixel(x + column, y + row, red, green, blue, alpha, combine);
			}
		}
	}

	void EditableImage::DrawImage(
		Vector2 position, std::shared_ptr<EditableImage> image, Enums::ImageCombineType combine
	) {
		if (!image || image->Width <= 0 || image->Height <= 0) {
			return;
		}

		Revision++;
		int originX = (int)glm::round(position.GetX());
		int originY = (int)glm::round(position.GetY());

		for (int y = 0; y < image->Height; y++) {
			for (int x = 0; x < image->Width; x++) {
				const uint8_t *source = image->Pixels.data() + ((size_t)y * image->Width + x) * CHANNELS;
				CombinePixel(originX + x, originY + y, source[0], source[1], source[2], source[3], combine);
			}
		}
	}

	void EditableImage::DrawCircle(
		Vector2 centre, float radius, Color3 color, float transparency, Enums::ImageCombineType combine
	) {
		if (radius <= 0.0f) {
			return;
		}

		Revision++;
		uint8_t r = (uint8_t)glm::round(glm::clamp(color.R, 0.0f, 1.0f) * 255.0f);
		uint8_t g = (uint8_t)glm::round(glm::clamp(color.G, 0.0f, 1.0f) * 255.0f);
		uint8_t b = (uint8_t)glm::round(glm::clamp(color.B, 0.0f, 1.0f) * 255.0f);
		uint8_t alpha = (uint8_t)glm::round((1.0f - glm::clamp(transparency, 0.0f, 1.0f)) * 255.0f);

		float centreX = centre.GetX();
		float centreY = centre.GetY();

		// Only the rows and columns the circle can touch
		int minX = glm::max(0, (int)glm::floor(centreX - radius));
		int maxX = glm::min(Width - 1, (int)glm::ceil(centreX + radius));
		int minY = glm::max(0, (int)glm::floor(centreY - radius));
		int maxY = glm::min(Height - 1, (int)glm::ceil(centreY + radius));

		// An integer coordinate names a pixel, so coverage is measured from the
		// pixel itself. This is what makes a one-pixel line cover its endpoints.
		for (int y = minY; y <= maxY; y++) {
			for (int x = minX; x <= maxX; x++) {
				CombinePixel(x, y, r, g, b, alpha, combine, DiscCoverage(x, y, centreX, centreY, radius));
			}
		}
	}

	void EditableImage::DrawLine(
		Vector2 from,
		Vector2 to,
		Color3 color,
		float transparency,
		float thickness,
		Enums::ImageCombineType combine
	) {
		Revision++;
		uint8_t r = (uint8_t)glm::round(glm::clamp(color.R, 0.0f, 1.0f) * 255.0f);
		uint8_t g = (uint8_t)glm::round(glm::clamp(color.G, 0.0f, 1.0f) * 255.0f);
		uint8_t b = (uint8_t)glm::round(glm::clamp(color.B, 0.0f, 1.0f) * 255.0f);
		uint8_t alpha = (uint8_t)glm::round((1.0f - glm::clamp(transparency, 0.0f, 1.0f)) * 255.0f);

		float radius = glm::max(thickness, 1.0f) / 2.0f;
		float startX = from.GetX();
		float startY = from.GetY();
		float deltaX = to.GetX() - startX;
		float deltaY = to.GetY() - startY;
		float length = glm::sqrt(deltaX * deltaX + deltaY * deltaY);

		// A zero-length line is just a dot
		if (length < 1e-4f) {
			DrawCircle(from, radius, color, transparency, combine);
			return;
		}

		// Cover the segment once, rather than stamping overlapping discs. That
		// keeps a translucent line from darkening where the stamps overlapped,
		// and lets each pixel be antialiased exactly once.
		glm::vec2 start{startX, startY};
		glm::vec2 end{to.GetX(), to.GetY()};

		int minX = glm::max(0, (int)glm::floor(glm::min(start.x, end.x) - radius - 1.0f));
		int maxX = glm::min(Width - 1, (int)glm::ceil(glm::max(start.x, end.x) + radius + 1.0f));
		int minY = glm::max(0, (int)glm::floor(glm::min(start.y, end.y) - radius - 1.0f));
		int maxY = glm::min(Height - 1, (int)glm::ceil(glm::max(start.y, end.y) + radius + 1.0f));

		for (int y = minY; y <= maxY; y++) {
			for (int x = minX; x <= maxX; x++) {
				CombinePixel(x, y, r, g, b, alpha, combine, SegmentCoverage(x, y, start, end, radius));
			}
		}
	}

	Enums::SaveFormat EditableImage::GuessSaveFormat(const std::string &path) {
		std::string extension = std::filesystem::path(path).extension().string();
		for (char &character : extension) {
			character = (char)std::tolower((unsigned char)character);
		}

		if (extension == ".jpg" || extension == ".jpeg") {
			return Enums::SaveFormat::JPG;
		}
		if (extension == ".bmp") {
			return Enums::SaveFormat::BMP;
		}
		// Anything else, including no extension at all, gets the format that
		// loses nothing
		return Enums::SaveFormat::PNG;
	}

	bool EditableImage::Save(std::string path, Enums::SaveFormat format) {
		if (Width <= 0 || Height <= 0) {
			SaveError = "The image is empty";
			return false;
		}

		std::filesystem::path resolved{path};
		if (resolved.is_relative()) {
			resolved = Paths::GetExecutableDirectory() / resolved;
		}

		std::error_code ignored;
		std::filesystem::create_directories(resolved.parent_path(), ignored);

		bool written = false;
		switch (format) {
		case Enums::SaveFormat::JPG:
			written = ImageDecoder::WriteJpg(resolved.string(), Width, Height, Pixels.data());
			break;
		case Enums::SaveFormat::BMP:
			written = ImageDecoder::WriteBmp(resolved.string(), Width, Height, Pixels.data());
			break;
		case Enums::SaveFormat::PNG:
		default:
			written = ImageDecoder::WritePng(resolved.string(), Width, Height, Pixels.data());
			break;
		}

		if (!written) {
			SaveError = "Could not write " + resolved.string();
			return false;
		}

		SaveError.clear();
		return true;
	}

	int EditableImage::LSave(lua_State *L, Instance *instance) {
		auto *image = instance->Cast<EditableImage>();
		if (!image) {
			luaL_error(L, "Save must be called on an EditableImage");
			return 0;
		}

		std::string path = CheckStackValue<std::string>(L, 2);
		// Left off, the path is asked what it wants. Given, it is obeyed even
		// when the extension disagrees, since naming the format is a clearer
		// statement of intent than naming the file.
		Enums::SaveFormat format =
			lua_isnoneornil(L, 3) ? GuessSaveFormat(path) : CheckStackValue<Enums::SaveFormat>(L, 3);

		StackValue<bool>::Push(L, image->Save(path, format));
		return 1;
	}

	std::string EditableImage::GetSaveError() const {
		return SaveError;
	}

	bool EditableImage::Load(std::string path) {
		auto decoded = ImageDecoder::DecodeFile(path);
		if (!decoded.Success) {
			LoadError = decoded.Error;
			return false;
		}

		LoadError.clear();
		SetPixels(decoded.Width, decoded.Height, decoded.Pixels.data());
		return true;
	}

	std::string EditableImage::GetLoadError() const {
		return LoadError;
	}

	int EditableImage::LReadPixelsBuffer(lua_State *L, Instance *instance) {
		auto *image = instance->Cast<EditableImage>();
		if (!image) {
			luaL_error(L, "ReadPixelsBuffer must be called on an EditableImage");
			return 0;
		}

		Vector2 position = CheckStackValue<Vector2>(L, 2);
		Vector2 size = CheckStackValue<Vector2>(L, 3);

		int requestedWidth = (int)glm::round(size.GetX());
		int requestedHeight = (int)glm::round(size.GetY());
		if (requestedWidth < 0 || requestedHeight < 0) {
			luaL_error(L, "ReadPixelsBuffer size cannot be negative");
			return 0;
		}

		// The returned buffer always matches the requested size; anything
		// outside the image stays zeroed rather than shifting the pixels
		size_t bytes = (size_t)requestedWidth * requestedHeight * CHANNELS;
		void *data = lua_newbuffer(L, bytes);
		std::memset(data, 0, bytes);

		int x = 0, y = 0, width = 0, height = 0;
		if (!image->ClampRegion(position, size, x, y, width, height)) {
			return 1;
		}

		int offsetX = x - (int)glm::round(position.GetX());
		int offsetY = y - (int)glm::round(position.GetY());

		auto *destination = static_cast<uint8_t *>(data);
		for (int row = 0; row < height; row++) {
			const uint8_t *source = image->Pixels.data() + ((size_t)(y + row) * image->Width + x) * CHANNELS;
			uint8_t *target =
				destination + ((size_t)(offsetY + row) * requestedWidth + offsetX) * CHANNELS;
			std::memcpy(target, source, (size_t)width * CHANNELS);
		}

		return 1;
	}

	int EditableImage::LWritePixelsBuffer(lua_State *L, Instance *instance) {
		auto *image = instance->Cast<EditableImage>();
		if (!image) {
			luaL_error(L, "WritePixelsBuffer must be called on an EditableImage");
			return 0;
		}

		Vector2 position = CheckStackValue<Vector2>(L, 2);
		Vector2 size = CheckStackValue<Vector2>(L, 3);
		LuauBuffer pixels = CheckStackValue<LuauBuffer>(L, 4);
		image->Revision++;

		int requestedWidth = (int)glm::round(size.GetX());
		int requestedHeight = (int)glm::round(size.GetY());
		size_t required = (size_t)glm::max(requestedWidth, 0) * glm::max(requestedHeight, 0) * CHANNELS;

		if (pixels.Size < required) {
			luaL_error(
				L,
				"WritePixelsBuffer needs a buffer of at least %d bytes for a %dx%d region, got %d",
				(int)required,
				requestedWidth,
				requestedHeight,
				(int)pixels.Size
			);
			return 0;
		}

		int x = 0, y = 0, width = 0, height = 0;
		if (!image->ClampRegion(position, size, x, y, width, height)) {
			return 0;
		}

		int offsetX = x - (int)glm::round(position.GetX());
		int offsetY = y - (int)glm::round(position.GetY());

		const auto *source = static_cast<const uint8_t *>(pixels.Data);
		for (int row = 0; row < height; row++) {
			const uint8_t *sourceRow =
				source + ((size_t)(offsetY + row) * requestedWidth + offsetX) * CHANNELS;
			uint8_t *destination = image->Pixels.data() + ((size_t)(y + row) * image->Width + x) * CHANNELS;
			std::memcpy(destination, sourceRow, (size_t)width * CHANNELS);
		}

		return 0;
	}
} // namespace gargantuan
