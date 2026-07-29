#include "gargantuan/classes/ShaderProperties.hpp"
// Kept here to break the header cycle while providing the complete types.
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <lualib.h>
#include <utility>

// Shared reflected signatures for parameter and texture names.
#define G_SHADER_NAME_TYPE "string | Enum.ShaderProperty"
#define SET_SIGNATURE(valueType) "(self, name: " G_SHADER_NAME_TYPE ", value: " valueType "): ()"
#define GET_SIGNATURE(returnType) "(self, name: " G_SHADER_NAME_TYPE "): " returnType

namespace gargantuan {
	const ShaderProperties::ClassDefinition ShaderProperties::DEFINITION = {
		.Name = "ShaderProperties",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<ShaderProperties>(),
		.Methods = {
			// Names accept strings or ShaderProperty; runtime shaders may add names.
			{"SetNumber", {&ShaderProperties::LSetNumber, []() -> std::string { return SET_SIGNATURE("number"); }}},
			{"SetVector2", {&ShaderProperties::LSetVector2, []() -> std::string { return SET_SIGNATURE("Vector2"); }}},
			{"SetVector3", {&ShaderProperties::LSetVector3, []() -> std::string { return SET_SIGNATURE("Vector3"); }}},
			{"SetColor3", {&ShaderProperties::LSetColor3, []() -> std::string { return SET_SIGNATURE("Color3"); }}},
			{"SetBool", {&ShaderProperties::LSetBool, []() -> std::string { return SET_SIGNATURE("boolean"); }}},
			{"SetImage",
			 {&ShaderProperties::LSetImage, []() -> std::string { return SET_SIGNATURE("EditableImage"); }}},
			{"GetImage",
			 {&ShaderProperties::LGetImage, []() -> std::string { return GET_SIGNATURE("EditableImage?"); }}},
			{"SetCameraTexture",
			 {&ShaderProperties::LSetCameraTexture, []() -> std::string { return SET_SIGNATURE("Camera"); }}},
			{"GetCameraTexture",
			 {&ShaderProperties::LGetCameraTexture, []() -> std::string { return GET_SIGNATURE("Camera?"); }}},
			{"SetRenderTexture",
			 {&ShaderProperties::LSetRenderTexture,
			  []() -> std::string { return SET_SIGNATURE("Enum.RenderTexture"); }}},
			{"GetRenderTexture",
			 {&ShaderProperties::LGetRenderTexture,
			  []() -> std::string { return GET_SIGNATURE("Enum.RenderTexture"); }}},
			{"ListImages", Method::Wrap<&ShaderProperties::ListImages>()},
			{"ClearImages", Method::Wrap<&ShaderProperties::ClearImages>()},
			{"ListParameters", Method::Wrap<&ShaderProperties::ListParameters>()},
			{"ClearParameters", Method::Wrap<&ShaderProperties::ClearParameters>()},
		}
	};

	std::shared_ptr<ShaderScript> ShaderProperties::GetOwner() const {
		return Owner.lock();
	}

	void ShaderProperties::SetOwner(std::shared_ptr<ShaderScript> owner) {
		Owner = std::move(owner);
	}

	namespace {
		// Enforce declared names only after the owning shader reflects. An
		// unowned set has nothing to check against and accepts anything.
		ShaderProperties *CheckParameterName(lua_State *L, Instance *instance, const std::string &name) {
			auto *properties = instance->Cast<ShaderProperties>();
			if (!properties) {
				luaL_error(L, "expected a ShaderProperties");
				return nullptr;
			}

			auto owner = properties->GetOwner();
			if (!owner) {
				return properties;
			}

			owner->Reflect();
			if (owner->IsParameterExpected(name)) {
				return properties;
			}

			std::string expected;
			for (const auto &declared : owner->GetExpectedParameters()) {
				expected += expected.empty() ? "" : ", ";
				expected += declared;
			}

			luaL_error(
				L,
				"'%s' is not a parameter of this shader. It declares: %s",
				name.c_str(),
				expected.empty() ? "(none)" : expected.c_str()
			);
			return nullptr;
		}

		// Texture names preserve binding order but are not reflected here.
		ShaderProperties *CheckProperties(lua_State *L, Instance *instance) {
			auto *properties = instance->Cast<ShaderProperties>();
			if (!properties) {
				luaL_error(L, "expected a ShaderProperties");
			}
			return properties;
		}
	}

	int ShaderProperties::LSetNumber(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		float value = CheckStackValue<float>(L, 3);
		if (auto *properties = CheckParameterName(L, instance, name)) {
			properties->SetNumber(name, value);
		}
		return 0;
	}

	int ShaderProperties::LSetVector2(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		Vector2 value = CheckStackValue<Vector2>(L, 3);
		if (auto *properties = CheckParameterName(L, instance, name)) {
			properties->SetVector2(name, value);
		}
		return 0;
	}

	int ShaderProperties::LSetVector3(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		glm::vec3 value = CheckStackValue<glm::vec3>(L, 3);
		if (auto *properties = CheckParameterName(L, instance, name)) {
			properties->SetVector3(name, value);
		}
		return 0;
	}

	int ShaderProperties::LSetColor3(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		Color3 value = CheckStackValue<Color3>(L, 3);
		if (auto *properties = CheckParameterName(L, instance, name)) {
			properties->SetColor3(name, value);
		}
		return 0;
	}

	int ShaderProperties::LSetBool(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		bool value = CheckStackValue<bool>(L, 3);
		if (auto *properties = CheckParameterName(L, instance, name)) {
			properties->SetBool(name, value);
		}
		return 0;
	}

	int ShaderProperties::LSetImage(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		auto image = CheckStackValue<std::shared_ptr<EditableImage>>(L, 3);
		if (auto *properties = CheckProperties(L, instance)) {
			properties->SetImage(std::move(name), std::move(image));
		}
		return 0;
	}

	int ShaderProperties::LGetImage(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		if (auto *properties = CheckProperties(L, instance)) {
			return StackValue<std::shared_ptr<EditableImage>>::Push(L, properties->GetImage(std::move(name)));
		}
		return 0;
	}

	int ShaderProperties::LSetCameraTexture(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		auto camera = CheckStackValue<std::shared_ptr<Camera>>(L, 3);
		if (auto *properties = CheckProperties(L, instance)) {
			properties->SetCameraTexture(std::move(name), std::move(camera));
		}
		return 0;
	}

	int ShaderProperties::LGetCameraTexture(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		if (auto *properties = CheckProperties(L, instance)) {
			return StackValue<std::shared_ptr<Camera>>::Push(L, properties->GetCameraTexture(std::move(name)));
		}
		return 0;
	}

	int ShaderProperties::LSetRenderTexture(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		auto texture = CheckStackValue<Enums::RenderTexture>(L, 3);
		if (auto *properties = CheckProperties(L, instance)) {
			properties->SetRenderTexture(std::move(name), texture);
		}
		return 0;
	}

	int ShaderProperties::LGetRenderTexture(lua_State *L, Instance *instance) {
		std::string name = CheckShaderPropertyArgument(L, 2);
		if (auto *properties = CheckProperties(L, instance)) {
			return StackValue<Enums::RenderTexture>::Push(L, properties->GetRenderTexture(std::move(name)));
		}
		return 0;
	}

	void ShaderProperties::SetParameter(const std::string &name, glm::vec4 value) {
		auto existing = ParameterIndices.find(name);
		if (existing != ParameterIndices.end()) {
			ParameterValues[existing->second] = value;
			return;
		}

		// Refuse overflow rather than corrupting the fixed uniform buffer.
		if (ParameterOrder.size() >= MAXIMUM_PARAMETERS) {
			return;
		}

		ParameterIndices.emplace(name, ParameterValues.size());
		ParameterOrder.push_back(name);
		ParameterValues.push_back(value);
	}

	void ShaderProperties::SetNumber(std::string name, float value) {
		SetParameter(name, glm::vec4(value, 0.0f, 0.0f, 0.0f));
	}

	void ShaderProperties::SetVector2(std::string name, Vector2 value) {
		SetParameter(name, glm::vec4(value.GetX(), value.GetY(), 0.0f, 0.0f));
	}

	void ShaderProperties::SetVector3(std::string name, glm::vec3 value) {
		SetParameter(name, glm::vec4(value, 0.0f));
	}

	void ShaderProperties::SetColor3(std::string name, Color3 value) {
		SetParameter(name, glm::vec4(value.R, value.G, value.B, 1.0f));
	}

	void ShaderProperties::SetBool(std::string name, bool value) {
		SetParameter(name, glm::vec4(value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));
	}

	namespace {
		// Replacement preserves sampler position.
		bool AssignTextureSource(
			std::vector<std::string> &order,
			std::unordered_map<std::string, ShaderProperties::TextureSource> &sources,
			const std::string &name,
			ShaderProperties::TextureSource source,
			size_t maximum
		) {
			auto existing = sources.find(name);
			if (existing != sources.end()) {
				existing->second = std::move(source);
				return true;
			}

			if (order.size() >= maximum) {
				return false;
			}

			order.push_back(name);
			sources.emplace(name, std::move(source));
			return true;
		}
	}

	void ShaderProperties::SetImage(std::string name, std::shared_ptr<EditableImage> image) {
		AssignTextureSource(ImageOrder, Images, name, TextureSource{std::move(image), nullptr}, MAXIMUM_IMAGES);
	}

	std::shared_ptr<EditableImage> ShaderProperties::GetImage(std::string name) const {
		auto it = Images.find(name);
		return it == Images.end() ? nullptr : it->second.Image;
	}

	void ShaderProperties::SetCameraTexture(std::string name, std::shared_ptr<Camera> camera) {
		AssignTextureSource(ImageOrder, Images, name, TextureSource{nullptr, std::move(camera)}, MAXIMUM_IMAGES);
	}

	std::shared_ptr<Camera> ShaderProperties::GetCameraTexture(std::string name) const {
		auto it = Images.find(name);
		return it == Images.end() ? nullptr : it->second.Camera;
	}

	void ShaderProperties::SetRenderTexture(std::string name, Enums::RenderTexture texture) {
		AssignTextureSource(ImageOrder, Images, name, TextureSource{nullptr, nullptr, texture}, MAXIMUM_IMAGES);
	}

	Enums::RenderTexture ShaderProperties::GetRenderTexture(std::string name) const {
		auto it = Images.find(name);
		return it == Images.end() ? Enums::RenderTexture::None : it->second.Render;
	}

	std::vector<ShaderProperties::TextureSource> ShaderProperties::GetTextureSources() const {
		std::vector<TextureSource> result;
		result.reserve(ImageOrder.size());
		for (const auto &name : ImageOrder) {
			auto it = Images.find(name);
			result.push_back(it == Images.end() ? TextureSource{} : it->second);
		}
		return result;
	}

	std::vector<std::string> ShaderProperties::ListImages() {
		return ImageOrder;
	}

	void ShaderProperties::ClearImages() {
		ImageOrder.clear();
		Images.clear();
	}

	std::vector<std::shared_ptr<EditableImage>> ShaderProperties::GetImages() const {
		std::vector<std::shared_ptr<EditableImage>> result;
		result.reserve(ImageOrder.size());
		for (const auto &name : ImageOrder) {
			auto it = Images.find(name);
			result.push_back(it == Images.end() ? nullptr : it->second.Image);
		}
		return result;
	}

	std::vector<std::string> ShaderProperties::ListParameters() {
		return ParameterOrder;
	}

	void ShaderProperties::ClearParameters() {
		ParameterOrder.clear();
		ParameterIndices.clear();
		ParameterValues.clear();
	}

	std::vector<std::pair<std::string, glm::vec4>> ShaderProperties::GetParameters() const {
		std::vector<std::pair<std::string, glm::vec4>> result;
		result.reserve(ParameterOrder.size());
		for (size_t index = 0; index < ParameterOrder.size(); index++) {
			result.emplace_back(ParameterOrder[index], ParameterValues[index]);
		}
		return result;
	}

	const std::vector<glm::vec4> &ShaderProperties::GetPackedParameters() const {
		return ParameterValues;
	}

	size_t ShaderProperties::GetPackedParameterBytes() const {
		return ParameterValues.size() * sizeof(glm::vec4);
	}
}
