#pragma once

#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/render/ShaderPresets.hpp"

#include <cstddef>
#include <glm/glm.hpp>
#include <lua.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gargantuan {
	class Camera;
	class ShaderScript;

	class ShaderProperties : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		static constexpr size_t MAXIMUM_PARAMETERS = 64;
		static constexpr size_t MAXIMUM_IMAGES = 8;

		struct TextureSource {
			std::shared_ptr<EditableImage> Image;
			std::shared_ptr<Camera> Camera;
			Enums::RenderTexture Render = Enums::RenderTexture::None;
		};

		void SetNumber(std::string name, float value);
		void SetVector2(std::string name, Vector2 value);
		void SetVector3(std::string name, glm::vec3 value);
		void SetColor3(std::string name, Color3 value);
		void SetBool(std::string name, bool value);

		void SetImage(std::string name, std::shared_ptr<EditableImage> image);
		std::shared_ptr<EditableImage> GetImage(std::string name) const;
		void SetCameraTexture(std::string name, std::shared_ptr<Camera> camera);
		std::shared_ptr<Camera> GetCameraTexture(std::string name) const;
		void SetRenderTexture(std::string name, Enums::RenderTexture texture);
		Enums::RenderTexture GetRenderTexture(std::string name) const;

		std::vector<std::string> ListImages();
		void ClearImages();
		std::vector<std::string> ListParameters();
		void ClearParameters();

		std::vector<TextureSource> GetTextureSources() const;

		std::vector<std::pair<std::string, glm::vec4>> GetParameters() const;

		const std::vector<glm::vec4> &GetPackedParameters() const;

		std::shared_ptr<ShaderScript> GetOwner() const;
		void SetOwner(std::shared_ptr<ShaderScript> owner);

		static int LSetNumber(lua_State *L, Instance *instance);
		static int LSetVector2(lua_State *L, Instance *instance);
		static int LSetVector3(lua_State *L, Instance *instance);
		static int LSetColor3(lua_State *L, Instance *instance);
		static int LSetBool(lua_State *L, Instance *instance);
		static int LSetImage(lua_State *L, Instance *instance);
		static int LGetImage(lua_State *L, Instance *instance);
		static int LSetCameraTexture(lua_State *L, Instance *instance);
		static int LGetCameraTexture(lua_State *L, Instance *instance);
		static int LSetRenderTexture(lua_State *L, Instance *instance);
		static int LGetRenderTexture(lua_State *L, Instance *instance);

	  private:
		void SetParameter(const std::string &name, glm::vec4 value);

		std::vector<std::string> ParameterOrder;
		std::unordered_map<std::string, size_t> ParameterIndices;
		std::vector<glm::vec4> ParameterValues;

		std::vector<std::string> ImageOrder;
		std::unordered_map<std::string, TextureSource> Images;

		std::weak_ptr<ShaderScript> Owner;
	};
}
