#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/scripting/Userdata.hpp"

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
			{"SetNumber", Method::Wrap<&ShaderScript::SetNumber>()},
			{"SetVector2", Method::Wrap<&ShaderScript::SetVector2>()},
			{"SetVector3", Method::Wrap<&ShaderScript::SetVector3>()},
			{"SetColor3", Method::Wrap<&ShaderScript::SetColor3>()},
			{"SetBool", Method::Wrap<&ShaderScript::SetBool>()},
			{"SetImage", Method::Wrap<&ShaderScript::SetImage>()},
			{"GetImage", Method::Wrap<&ShaderScript::GetImage>()},
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

	void ShaderScript::SetImage(std::shared_ptr<EditableImage> image) {
		Image = std::move(image);
	}

	std::shared_ptr<EditableImage> ShaderScript::GetImage() const {
		return Image;
	}

	std::vector<std::string> ShaderScript::ListParameters() {
		return ParameterOrder;
	}

	void ShaderScript::ClearParameters() {
		ParameterOrder.clear();
		ParameterIndices.clear();
		ParameterValues.clear();
	}

	const std::vector<glm::vec4> &ShaderScript::GetPackedParameters() const {
		return ParameterValues;
	}

	size_t ShaderScript::GetPackedParameterBytes() const {
		return ParameterValues.size() * sizeof(glm::vec4);
	}
} // namespace gargantuan
