#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/scripting/LuauBuffer.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <algorithm>
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

	void EditableImage::DrawRectangle(Vector2 position, Vector2 size, Color3 color, float transparency) {
		Revision++;
		int x = 0, y = 0, width = 0, height = 0;
		if (!ClampRegion(position, size, x, y, width, height)) {
			return;
		}

		uint8_t red = (uint8_t)glm::round(glm::clamp(color.R, 0.0f, 1.0f) * 255.0f);
		uint8_t green = (uint8_t)glm::round(glm::clamp(color.G, 0.0f, 1.0f) * 255.0f);
		uint8_t blue = (uint8_t)glm::round(glm::clamp(color.B, 0.0f, 1.0f) * 255.0f);
		uint8_t alpha = (uint8_t)glm::round((1.0f - glm::clamp(transparency, 0.0f, 1.0f)) * 255.0f);

		for (int row = 0; row < height; row++) {
			uint8_t *pixel = Pixels.data() + ((size_t)(y + row) * Width + x) * CHANNELS;
			for (int column = 0; column < width; column++) {
				pixel[0] = red;
				pixel[1] = green;
				pixel[2] = blue;
				pixel[3] = alpha;
				pixel += CHANNELS;
			}
		}
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
