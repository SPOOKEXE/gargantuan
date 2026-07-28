#include "gargantuan/classes/ShaderScript.hpp"
// Camera.hpp includes this header back, so it is pulled in here rather than
// there; SetCameraTexture needs the complete type to marshal it
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <lualib.h>

namespace gargantuan {
	const ShaderScript::ClassDefinition ShaderScript::DEFINITION = {
		.Name = "ShaderScript",
		.Superclass = "Instance",
		.Properties =
			{
				G_UD_READWRITE_PROP(ShaderScript, Source, std::string),
				{
					"Code",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<ShaderScript>()->GetCode());
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<ShaderScript>()->SetCode(CheckStackValue<std::string>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(std::string),
					},
				},
				{
					"CompileError",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<ShaderScript>()->GetCompileError());
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(std::string),
					},
				},
			},
		.Methods = {
			{"SetNumber",
			 {&ShaderScript::LSetNumber, []() -> std::string { return "(self, name: string, value: number): ()"; }}},
			{"SetVector2",
			 {&ShaderScript::LSetVector2, []() -> std::string { return "(self, name: string, value: Vector2): ()"; }}},
			{"SetVector3",
			 {&ShaderScript::LSetVector3, []() -> std::string { return "(self, name: string, value: Vector3): ()"; }}},
			{"SetColor3",
			 {&ShaderScript::LSetColor3, []() -> std::string { return "(self, name: string, value: Color3): ()"; }}},
			{"SetBool",
			 {&ShaderScript::LSetBool, []() -> std::string { return "(self, name: string, value: boolean): ()"; }}},
			{"SetImage", Method::Wrap<&ShaderScript::SetImage>()},
			{"GetImage", Method::Wrap<&ShaderScript::GetImage>()},
			{"SetCameraTexture", Method::Wrap<&ShaderScript::SetCameraTexture>()},
			{"GetCameraTexture", Method::Wrap<&ShaderScript::GetCameraTexture>()},
			{"ListImages", Method::Wrap<&ShaderScript::ListImages>()},
			{"ClearImages", Method::Wrap<&ShaderScript::ClearImages>()},
			{"GetExpectedParameters", Method::Wrap<&ShaderScript::GetExpectedParameters>()},
			{"Reflect", Method::Wrap<&ShaderScript::Reflect>()},
			{"ListParameters", Method::Wrap<&ShaderScript::ListParameters>()},
			{"ClearParameters", Method::Wrap<&ShaderScript::ClearParameters>()},
			{"Compile", Method::Wrap<&ShaderScript::Compile>()},
			{"Validate", Method::Wrap<&ShaderScript::Validate>()},
		}
	};

	std::string ShaderScript::GetCode() const {
		return Code;
	}

	void ShaderScript::SetCode(std::string code) {
		if (Code == code) {
			return;
		}

		Code = std::move(code);
		// The declared parameters belong to the old code
		Reflected = false;
		DeclaredParameters = {};
		// The old bytecode no longer matches the source, so drop it and make
		// the renderer notice
		Bytecode.clear();
		CompileError.clear();
		Revision++;
	}

	bool ShaderScript::Compile() {
		auto result = ShaderCompiler::Compile(Code, GetStage(), std::string(Name));
		CompileError = result.Error;

		if (!result.Success) {
			Bytecode.clear();
			Revision++;
			return false;
		}

		Bytecode = std::move(result.Bytecode);
		Revision++;
		Reflected = false;
		Reflect();
		return true;
	}

	bool ShaderScript::Validate() {
		auto result = ShaderCompiler::Validate(Code, GetStage(), std::string(Name));
		CompileError = result.Error;
		return result.Success;
	}

	std::string ShaderScript::GetCompileError() const {
		return CompileError;
	}

	bool ShaderScript::HasBytecode() const {
		return !Bytecode.empty();
	}

	const std::vector<unsigned char> &ShaderScript::GetBytecode() const {
		return Bytecode;
	}

	uint64_t ShaderScript::GetRevision() const {
		return Revision;
	}

	namespace {
		// Rejects a name the shader never declared, listing what it does take.
		// Only enforced once the layout is known; before that anything goes,
		// because the shader may not have been compiled yet.
		ShaderScript *CheckParameterName(lua_State *L, Instance *instance, const std::string &name) {
			auto *shader = instance->Cast<ShaderScript>();
			if (!shader) {
				luaL_error(L, "expected a ShaderScript");
				return nullptr;
			}

			shader->Reflect();
			if (shader->IsParameterExpected(name)) {
				return shader;
			}

			std::string expected;
			for (const auto &declared : shader->GetExpectedParameters()) {
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
	} // namespace

	int ShaderScript::LSetNumber(lua_State *L, Instance *instance) {
		std::string name = CheckStackValue<std::string>(L, 2);
		float value = CheckStackValue<float>(L, 3);
		if (auto *shader = CheckParameterName(L, instance, name)) {
			shader->SetNumber(name, value);
		}
		return 0;
	}

	int ShaderScript::LSetVector2(lua_State *L, Instance *instance) {
		std::string name = CheckStackValue<std::string>(L, 2);
		Vector2 value = CheckStackValue<Vector2>(L, 3);
		if (auto *shader = CheckParameterName(L, instance, name)) {
			shader->SetVector2(name, value);
		}
		return 0;
	}

	int ShaderScript::LSetVector3(lua_State *L, Instance *instance) {
		std::string name = CheckStackValue<std::string>(L, 2);
		glm::vec3 value = CheckStackValue<glm::vec3>(L, 3);
		if (auto *shader = CheckParameterName(L, instance, name)) {
			shader->SetVector3(name, value);
		}
		return 0;
	}

	int ShaderScript::LSetColor3(lua_State *L, Instance *instance) {
		std::string name = CheckStackValue<std::string>(L, 2);
		Color3 value = CheckStackValue<Color3>(L, 3);
		if (auto *shader = CheckParameterName(L, instance, name)) {
			shader->SetColor3(name, value);
		}
		return 0;
	}

	int ShaderScript::LSetBool(lua_State *L, Instance *instance) {
		std::string name = CheckStackValue<std::string>(L, 2);
		bool value = CheckStackValue<bool>(L, 3);
		if (auto *shader = CheckParameterName(L, instance, name)) {
			shader->SetBool(name, value);
		}
		return 0;
	}

	bool ShaderScript::Reflect() {
		if (Reflected) {
			return DeclaredParameters.Found;
		}

		// Runtime code is already in hand; a named asset has to be read back off
		// disk, the same file the renderer would load
		if (HasBytecode()) {
			DeclaredParameters = ShaderReflection::ReflectUniformBlock(Bytecode.data(), Bytecode.size(), 1);
			Reflected = true;
			return DeclaredParameters.Found;
		}

		if (Source.empty()) {
			return false;
		}

		const char *stageExtension = GetStage() == ShaderCompiler::Stage::Compute ? ".comp" : ".frag";
		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string bytecodeExtension, entrypoint;

		// Without a GPU there is no way to know which bytecode flavour to read,
		// so assume SPIR-V, which is what the build produces everywhere but Apple
		bytecodeExtension = ".spv";

		auto path = GetShaderPath(Source + stageExtension).string() + bytecodeExtension;
		size_t size = 0;
		void *code = SDL_LoadFile(path.c_str(), &size);
		if (!code) {
			return false;
		}

		DeclaredParameters = ShaderReflection::ReflectUniformBlock(code, size, 1);
		SDL_free(code);
		Reflected = true;
		return DeclaredParameters.Found;
	}

	bool ShaderScript::IsReflected() const {
		return Reflected && DeclaredParameters.Found;
	}

	bool ShaderScript::IsParameterExpected(const std::string &name) const {
		return !IsReflected() || DeclaredParameters.Find(name) != nullptr;
	}

	std::vector<std::string> ShaderScript::GetExpectedParameters() {
		Reflect();

		std::vector<std::string> names;
		names.reserve(DeclaredParameters.Members.size());
		for (const auto &[name, _] : DeclaredParameters.Members) {
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	void ShaderScript::SetParameter(const std::string &name, glm::vec4 value) {
		auto existing = ParameterIndices.find(name);
		if (existing != ParameterIndices.end()) {
			ParameterValues[existing->second] = value;
			return;
		}

		// Silently growing past the uniform buffer would corrupt the ones that
		// fit, so refuse the new parameter instead
		if (ParameterOrder.size() >= MAXIMUM_PARAMETERS) {
			return;
		}

		ParameterIndices.emplace(name, ParameterValues.size());
		ParameterOrder.push_back(name);
		ParameterValues.push_back(value);
	}

	void ShaderScript::SetNumber(std::string name, float value) {
		SetParameter(name, glm::vec4(value, 0.0f, 0.0f, 0.0f));
	}

	void ShaderScript::SetVector2(std::string name, Vector2 value) {
		SetParameter(name, glm::vec4(value.GetX(), value.GetY(), 0.0f, 0.0f));
	}

	void ShaderScript::SetVector3(std::string name, glm::vec3 value) {
		SetParameter(name, glm::vec4(value, 0.0f));
	}

	void ShaderScript::SetColor3(std::string name, Color3 value) {
		SetParameter(name, glm::vec4(value.R, value.G, value.B, 1.0f));
	}

	void ShaderScript::SetBool(std::string name, bool value) {
		SetParameter(name, glm::vec4(value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));
	}

	namespace {
		// Replacing a binding keeps its position, so the ones after it do not
		// silently shift to a different sampler slot
		bool AssignTextureSource(
			std::vector<std::string> &order,
			std::unordered_map<std::string, ShaderScript::TextureSource> &sources,
			const std::string &name,
			ShaderScript::TextureSource source,
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
	} // namespace

	void ShaderScript::SetImage(std::string name, std::shared_ptr<EditableImage> image) {
		AssignTextureSource(ImageOrder, Images, name, TextureSource{std::move(image), nullptr}, MAXIMUM_IMAGES);
	}

	std::shared_ptr<EditableImage> ShaderScript::GetImage(std::string name) const {
		auto it = Images.find(name);
		return it == Images.end() ? nullptr : it->second.Image;
	}

	void ShaderScript::SetCameraTexture(std::string name, std::shared_ptr<Camera> camera) {
		AssignTextureSource(ImageOrder, Images, name, TextureSource{nullptr, std::move(camera)}, MAXIMUM_IMAGES);
	}

	std::shared_ptr<Camera> ShaderScript::GetCameraTexture(std::string name) const {
		auto it = Images.find(name);
		return it == Images.end() ? nullptr : it->second.Camera;
	}

	std::vector<ShaderScript::TextureSource> ShaderScript::GetTextureSources() const {
		std::vector<TextureSource> result;
		result.reserve(ImageOrder.size());
		for (const auto &name : ImageOrder) {
			auto it = Images.find(name);
			result.push_back(it == Images.end() ? TextureSource{} : it->second);
		}
		return result;
	}

	std::vector<std::string> ShaderScript::ListImages() {
		return ImageOrder;
	}

	void ShaderScript::ClearImages() {
		ImageOrder.clear();
		Images.clear();
	}

	std::vector<std::shared_ptr<EditableImage>> ShaderScript::GetImages() const {
		std::vector<std::shared_ptr<EditableImage>> result;
		result.reserve(ImageOrder.size());
		for (const auto &name : ImageOrder) {
			auto it = Images.find(name);
			result.push_back(it == Images.end() ? nullptr : it->second.Image);
		}
		return result;
	}

	std::vector<std::string> ShaderScript::ListParameters() {
		return ParameterOrder;
	}

	void ShaderScript::ClearParameters() {
		ParameterOrder.clear();
		ParameterIndices.clear();
		ParameterValues.clear();
	}

	std::vector<std::pair<std::string, glm::vec4>> ShaderScript::GetParameters() const {
		std::vector<std::pair<std::string, glm::vec4>> result;
		result.reserve(ParameterOrder.size());
		for (size_t index = 0; index < ParameterOrder.size(); index++) {
			result.emplace_back(ParameterOrder[index], ParameterValues[index]);
		}
		return result;
	}

	const std::vector<glm::vec4> &ShaderScript::GetPackedParameters() const {
		return ParameterValues;
	}

	size_t ShaderScript::GetPackedParameterBytes() const {
		return ParameterValues.size() * sizeof(glm::vec4);
	}
} // namespace gargantuan
