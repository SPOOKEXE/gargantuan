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

	// A shader's parameter values and texture bindings, held apart from the
	// shader itself so one set can drive several of them.
	class ShaderProperties : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// Maximum parameter slots in one uniform buffer.
		static constexpr size_t MAXIMUM_PARAMETERS = 64;
		// Images bind in first-set order after SourceTexture at sampler slot 0.
		static constexpr size_t MAXIMUM_IMAGES = 8;

		// One bound texture: an image, another camera's output, or one of the
		// reader camera's own buffers
		struct TextureSource {
			std::shared_ptr<EditableImage> Image;
			std::shared_ptr<Camera> Camera;
			Enums::RenderTexture Render = Enums::RenderTexture::None;
		};

		// Parameters use 16-byte std140 slots in first-set order.
		void SetNumber(std::string name, float value);
		void SetVector2(std::string name, Vector2 value);
		void SetVector3(std::string name, glm::vec3 value);
		void SetColor3(std::string name, Color3 value);
		void SetBool(std::string name, bool value);

		void SetImage(std::string name, std::shared_ptr<EditableImage> image);
		std::shared_ptr<EditableImage> GetImage(std::string name) const;
		// Binds another camera's GPU output; that camera must render first.
		void SetCameraTexture(std::string name, std::shared_ptr<Camera> camera);
		std::shared_ptr<Camera> GetCameraTexture(std::string name) const;
		// Binds a buffer owned by the camera running the pass; requesting it enables production.
		void SetRenderTexture(std::string name, Enums::RenderTexture texture);
		Enums::RenderTexture GetRenderTexture(std::string name) const;

		std::vector<std::string> ListImages();
		void ClearImages();
		std::vector<std::string> ListParameters();
		void ClearParameters();

		// In binding order, for the renderer
		std::vector<std::shared_ptr<EditableImage>> GetImages() const;
		std::vector<TextureSource> GetTextureSources() const;

		// Name and value of every parameter, in the order they were first set
		std::vector<std::pair<std::string, glm::vec4>> GetParameters() const;

		// Packed slots, ready to push as uniform data. Only used when a shader's
		// layout could not be reflected.
		const std::vector<glm::vec4> &GetPackedParameters() const;
		size_t GetPackedParameterBytes() const;

		// The shader whose reflection parameter names are checked against.
		// Null until a shader hands these properties out or adopts them, and
		// weak so a destroyed shader stops being consulted.
		std::shared_ptr<ShaderScript> GetOwner() const;
		void SetOwner(std::shared_ptr<ShaderScript> owner);

		// Manual bindings validate names against the owner's reflection.
		static int LSetNumber(lua_State *L, Instance *instance);
		static int LSetVector2(lua_State *L, Instance *instance);
		static int LSetVector3(lua_State *L, Instance *instance);
		static int LSetColor3(lua_State *L, Instance *instance);
		static int LSetBool(lua_State *L, Instance *instance);
		// Bound by hand too, so a binding's name can be an Enum.ShaderProperty
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
