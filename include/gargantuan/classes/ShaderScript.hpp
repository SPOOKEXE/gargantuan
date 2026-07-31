#pragma once

#include "gargantuan/classes/ShaderProperties.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/render/ShaderCompiler.hpp"
#include "gargantuan/render/ShaderPresets.hpp"
#include "gargantuan/render/ShaderReflection.hpp"

#include <cstdint>
#include <lua.h>
#include <memory>
#include <string>
#include <vector>

namespace gargantuan {
	class ShaderScript : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		std::string SourceAssetName;

		bool RedrawEveryFrame = false;

		bool NeedsRedrawEveryFrame();
		bool ReadsBuiltinTime();

		bool JitterProjection = false;

		bool NeedsJitteredProjection();
		bool ReadsBuiltinJitter();

		std::string GetGLSLCode() const;
		void SetGLSLCode(std::string glslCode);

		bool Compile();
		bool Validate();
		std::string GetCompileError() const;

		bool HasBytecode() const;
		const std::vector<unsigned char> &GetBytecode() const;
		uint64_t GetRevision() const;
		uint64_t GetSerial() const;

		virtual ShaderCompiler::Stage GetStage() const = 0;

		const std::shared_ptr<ShaderProperties> &GetProperties();
		void SetProperties(std::shared_ptr<ShaderProperties> properties);

		std::vector<std::string> GetExpectedParameters();
		bool Reflect();
		bool IsReflected() const;
		bool IsParameterExpected(const std::string &name) const;

	  private:
		std::shared_ptr<ShaderProperties> Properties;

		ShaderReflection::BlockLayout DeclaredParameters;
		bool Reflected = false;

		bool ReadsTime = false;
		bool ReadsJitter = false;
		bool BuiltinsChecked = false;
		void CheckBuiltins(const void *spirv, size_t bytecodeByteCount);

		std::string GLSLCode;
		std::string CompileError;
		std::vector<unsigned char> Bytecode;
		uint64_t Revision = 0;

		static uint64_t NextSerial();
		const uint64_t Serial = NextSerial();
	};
}
