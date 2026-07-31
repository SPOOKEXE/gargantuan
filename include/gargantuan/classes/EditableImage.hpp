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
	G_ENUM(
		ImageCombineType,

		BlendSourceOver,
		Overwrite,
		Add,
		Multiply
	)

	G_ENUM(SaveFormat, PNG, JPG, BMP)

	class EditableImage : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		static constexpr int CHANNELS = 4;
		static constexpr int MAXIMUM_DIMENSION = 4096;

		std::vector<uint8_t> Pixels;

		EditableImage() = default;

		uint64_t GetRevision() const;

		int GetWidth() const;
		int GetHeight() const;
		Vector2 GetSize() const;

		void Resize(Vector2 size);
		void Crop(Vector2 minimum, Vector2 maximum);
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

		bool Save(std::string path, Enums::SaveFormat format);
		static Enums::SaveFormat GuessSaveFormat(const std::string &path);
		std::string GetSaveError() const;

		static int LSave(lua_State *L, Instance *instance);

		bool Load(std::string path);
		std::string GetLoadError() const;

		void SetPixels(int width, int height, const uint8_t *rgba);

		static int LReadPixelsBuffer(lua_State *L, Instance *instance);
		static int LWritePixelsBuffer(lua_State *L, Instance *instance);

	  private:
		int Width = 0;
		int Height = 0;
		uint64_t Revision = 1;
		std::string LoadError;
		std::string SaveError;

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

		bool ClampRegion(Vector2 position, Vector2 size, int &outX, int &outY, int &outWidth, int &outHeight) const;
	};
}
