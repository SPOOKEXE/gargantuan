#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/reflection/Enums.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <lua.h>
#include <vector>

namespace gargantuan {
	// How a Draw call combines with what is already there. Roblox names the
	// first two; the rest are the obvious useful ones.
	G_ENUM(
		ImageCombineType,

		// Alpha-blend the new pixels over the old, the usual behaviour
		BlendSourceOver,
		// Replace whatever was there, alpha included
		Overwrite,
		Add,
		Multiply
	)

	// A CPU-side RGBA8 image that Luau can read and write. Camera:Render()
	// produces one; later on the same object is what a Decal or ImageLabel
	// will point at, which is why it is an Instance rather than a datatype.
	class EditableImage : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		static constexpr int CHANNELS = 4;
		// Roblox caps editable images at 1024 on a side
		static constexpr int MAXIMUM_DIMENSION = 4096;

		// Row-major, top-left origin, four bytes per pixel.
		// Always exactly Width * Height * CHANNELS bytes.
		std::vector<uint8_t> Pixels;

		EditableImage() = default;

		// Bumped on every change, so a cached GPU upload can tell it is stale
		uint64_t GetRevision() const;

		int GetWidth() const;
		int GetHeight() const;
		Vector2 GetSize() const;

		// Reallocates and clears. Anything previously drawn is lost, matching
		// Roblox's Resize rather than a rescale.
		void Resize(Vector2 size);
		// Keeps the pixels inside the rectangle and drops the rest
		void Crop(Vector2 minimum, Vector2 maximum);
		// Every Draw call takes a combine mode; passing none blends over.
		// Circles and lines are antialiased along their edges, so they are not
		// the hard-edged staircases they used to be.
		void DrawRectangle(
			Vector2 position, Vector2 size, Color3 color, float transparency, Enums::ImageCombineType combine
		);
		void DrawImage(
			Vector2 position, std::shared_ptr<EditableImage> image, Enums::ImageCombineType combine
		);
		void DrawCircle(
			Vector2 centre, float radius, Color3 color, float transparency, Enums::ImageCombineType combine
		);
		void DrawLine(
			Vector2 from,
			Vector2 to,
			Color3 color,
			float transparency,
			float thickness,
			Enums::ImageCombineType combine
		);

		// Writes the image out as a PNG. Relative paths resolve against the
		// executable's directory, the same as Load.
		bool Save(std::string path);
		std::string GetSaveError() const;

		// Decodes a PNG, JPEG or other stb-supported file into this image.
		// Relative paths resolve against the executable's directory.
		bool Load(std::string path);
		// Why the last Load failed
		std::string GetLoadError() const;

		// Replaces the contents wholesale; used by the renderer's readback
		void SetPixels(int width, int height, const uint8_t *rgba);

		static int LReadPixelsBuffer(lua_State *L, Instance *instance);
		static int LWritePixelsBuffer(lua_State *L, Instance *instance);

	  private:
		int Width = 0;
		int Height = 0;
		uint64_t Revision = 1;
		std::string LoadError;
		std::string SaveError;

		// Combines one pixel, which every draw call goes through. `coverage` is
		// how much of the pixel the shape covers, 0 to 1, and is what makes the
		// edges smooth rather than stepped.
		void CombinePixel(
			int x,
			int y,
			uint8_t r,
			uint8_t g,
			uint8_t b,
			uint8_t a,
			Enums::ImageCombineType combine,
			float coverage = 1.0f
		);

		// Clamps a Luau-supplied rectangle to the image, returning false when
		// nothing of it overlaps
		bool ClampRegion(Vector2 position, Vector2 size, int &outX, int &outY, int &outWidth, int &outHeight) const;
	};
} // namespace gargantuan
