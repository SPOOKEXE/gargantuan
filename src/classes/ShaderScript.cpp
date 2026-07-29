#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/render/Shader.hpp"
#include "gargantuan/render/ShaderPresets.hpp"
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
				// Accepts a preset enum but reads back as its source string.
				{
					"Source",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<std::string>::Push(L, instance->Cast<ShaderScript>()->Source);
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<ShaderScript>()->Source = CheckPresetShaderArgument(L, -1);
							return 0;
						},
						[]() -> std::string { return "string | Enum.PresetShaders"; },
					},
				},
				G_UD_READWRITE_PROP(ShaderScript, RedrawEveryFrame, bool),
				G_UD_READWRITE_PROP(ShaderScript, JitterProjection, bool),
				{
					"Preset",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<Enums::PresetShaders>::Push(
								L, GetPresetShaderFromSource(instance->Cast<ShaderScript>()->Source)
							);
							return 1;
						},
						[](lua_State *L, Instance *instance) -> int {
							instance->Cast<ShaderScript>()->Source =
								GetPresetShaderSource(CheckStackValue<Enums::PresetShaders>(L, -1));
							return 0;
						},
						G_UD_REFLECT_TYPE(Enums::PresetShaders),
					},
				},
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
				// Assigning nil clears the set rather than leaving the shader without one.
				{
					"Properties",
					{
						[](lua_State *L, Instance *instance) -> int {
							return StackValue<std::shared_ptr<ShaderProperties>>::Push(
								L, instance->Cast<ShaderScript>()->GetProperties()
							);
						},
						[](lua_State *L, Instance *instance) -> int {
							auto properties = lua_isnoneornil(L, -1)
												  ? nullptr
												  : CheckStackValue<std::shared_ptr<ShaderProperties>>(L, -1);
							instance->Cast<ShaderScript>()->SetProperties(std::move(properties));
							return 0;
						},
						G_UD_REFLECT_TYPE(std::shared_ptr<ShaderProperties>),
					},
				},
			},
		.Methods = {
			{"GetExpectedParameters", Method::Wrap<&ShaderScript::GetExpectedParameters>()},
			{"Reflect", Method::Wrap<&ShaderScript::Reflect>()},
			{"Compile", Method::Wrap<&ShaderScript::Compile>()},
			{"Validate", Method::Wrap<&ShaderScript::Validate>()},
		}
	};

	const std::shared_ptr<ShaderProperties> &ShaderScript::GetProperties() {
		if (!Properties) {
			Properties = std::make_shared<ShaderProperties>();
			// Direct construction skips the registry constructor that names an instance.
			Properties->Name = ShaderProperties::DEFINITION.Name;
		}

		// Whichever shader a shared set was last reached through is the one its
		// parameter names are checked against.
		Properties->SetOwner(std::static_pointer_cast<ShaderScript>(weak_from_this().lock()));
		return Properties;
	}

	void ShaderScript::SetProperties(std::shared_ptr<ShaderProperties> properties) {
		Properties = std::move(properties);
		// Fills in a null set and claims ownership of the new one.
		GetProperties();
	}

	std::string ShaderScript::GetCode() const {
		return Code;
	}

	void ShaderScript::SetCode(std::string code) {
		if (Code == code) {
			return;
		}

		Code = std::move(code);
		// Invalidate reflection derived from the replaced code.
		Reflected = false;
		DeclaredParameters = {};
		BuiltinsChecked = false;
		ReadsTime = false;
		ReadsJitter = false;
		// Drop stale bytecode and invalidate the renderer's pipeline key.
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
			BuiltinsChecked = false;
			ReadsTime = false;
			ReadsJitter = false;
			return false;
		}

		Bytecode = std::move(result.Bytecode);
		Revision++;
		Reflected = false;
		BuiltinsChecked = false;
		ReadsTime = false;
		ReadsJitter = false;
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

	uint64_t ShaderScript::NextSerial() {
		static uint64_t next = 0;
		return ++next;
	}

	uint64_t ShaderScript::GetSerial() const {
		return Serial;
	}

	bool ShaderScript::Reflect() {
		if (Reflected) {
			return DeclaredParameters.Found;
		}

		// Runtime bytecode is in memory; named assets are reflected from disk.
		if (HasBytecode()) {
			DeclaredParameters = ShaderReflection::ReflectUniformBlock(Bytecode.data(), Bytecode.size(), 1);
			CheckBuiltins(Bytecode.data(), Bytecode.size());
			Reflected = true;
			return DeclaredParameters.Found;
		}

		if (Source.empty()) {
			// No source can appear without invalidating this result.
			BuiltinsChecked = true;
			return false;
		}

		const char *stageExtension = GetStage() == ShaderCompiler::Stage::Compute ? ".comp" : ".frag";
		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		std::string bytecodeExtension, entrypoint;

		// Reflection reads build-produced SPIR-V; Apple uses another runtime format.
		bytecodeExtension = ".spv";

		auto path = GetShaderPath(Source + stageExtension).string() + bytecodeExtension;
		size_t size = 0;
		void *code = SDL_LoadFile(path.c_str(), &size);
		if (!code) {
			// Cache unresolved assets to avoid a file miss each frame.
			BuiltinsChecked = true;
			return false;
		}

		DeclaredParameters = ShaderReflection::ReflectUniformBlock(code, size, 1);
		CheckBuiltins(code, size);
		SDL_free(code);
		Reflected = true;
		return DeclaredParameters.Found;
	}

	void ShaderScript::CheckBuiltins(const void *spirv, size_t bytes) {
		// Builtins use uniform binding 0; reflection also checks storage class.
		auto usage = ShaderReflection::ReflectBlockUsage(spirv, bytes, 0);
		ReadsTime = usage.Reads("Time");
		ReadsJitter = usage.Reads("Jitter");
		BuiltinsChecked = true;
	}

	bool ShaderScript::ReadsBuiltinTime() {
		if (!BuiltinsChecked) {
			Reflect();
		}
		return ReadsTime;
	}

	bool ShaderScript::ReadsBuiltinJitter() {
		if (!BuiltinsChecked) {
			Reflect();
		}
		return ReadsJitter;
	}

	bool ShaderScript::NeedsRedrawEveryFrame() {
		// Reflection can force redraws; the script flag cannot suppress them.
		return RedrawEveryFrame || ReadsBuiltinTime();
	}

	bool ShaderScript::NeedsJitteredProjection() {
		// Reflection can force jitter; the script flag cannot suppress it.
		return JitterProjection || ReadsBuiltinJitter();
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
}
