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
	// Camera shader base. Source names a build-time asset without its extension.
	// Runtime GLSL must be compiled to bytecode before SDL can use it.
	class ShaderScript : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// Asset name without directory or extension; compiled Code takes precedence.
		std::string Source;

		// Forces redraws; cannot override reflected builtin.Time reads.
		bool RedrawEveryFrame = false;

		// True when forced or builtin.Time is read; guards the cascading frame cache.
		bool NeedsRedrawEveryFrame();
		// True only when SPIR-V loads builtin.Time, not merely declares it.
		bool ReadsBuiltinTime();

		// Forces per-frame sub-pixel projection jitter; reflected Jitter reads also force it.
		bool JitterProjection = false;

		bool NeedsJitteredProjection();
		// Whether the shader reads builtin.Jitter, read out of its SPIR-V
		bool ReadsBuiltinJitter();

		// Runtime GLSL; Compile() produces bytecode or CompileError.
		std::string GetCode() const;
		void SetCode(std::string code);

		// Compiles Code; CompileError retains diagnostics, including warnings.
		bool Compile();
		// Compiles for validation without retaining bytecode.
		bool Validate();
		std::string GetCompileError() const;

		// True once Code has compiled and the bytecode is ready to use
		bool HasBytecode() const;
		const std::vector<unsigned char> &GetBytecode() const;
		// Bumped every time the bytecode changes, so the renderer knows when to
		// rebuild the pipeline it cached
		uint64_t GetRevision() const;
		// Process-unique, never reused; prevents pipeline-cache aliasing after destruction.
		uint64_t GetSerial() const;

		// Which stage this kind of shader compiles as
		virtual ShaderCompiler::Stage GetStage() const = 0;

		// Parameter values and texture bindings. Never null; reading it also
		// makes this shader the one its parameter names are checked against.
		const std::shared_ptr<ShaderProperties> &GetProperties();
		// A null set is replaced with a fresh empty one rather than stored.
		void SetProperties(std::shared_ptr<ShaderProperties> properties);

		// SPIR-V names. Compile/Validate reflect runtime code; assets reflect lazily.
		std::vector<std::string> GetExpectedParameters();
		// Reads the shader's declared layout. Safe to call repeatedly.
		bool Reflect();
		bool IsReflected() const;
		// True when a parameter of this name can be set
		bool IsParameterExpected(const std::string &name) const;

	  private:
		// Built on first use, because a shader constructed by the class
		// registry is not yet shared-owned and so cannot claim ownership.
		std::shared_ptr<ShaderProperties> Properties;

		ShaderReflection::BlockLayout DeclaredParameters;
		bool Reflected = false;

		// Cached until bytecode changes; independent of parameter-block reflection.
		bool ReadsTime = false;
		bool ReadsJitter = false;
		bool BuiltinsChecked = false;
		// Reads the builtin block's usage out of `spirv`, and records that the
		// question has now been asked
		void CheckBuiltins(const void *spirv, size_t bytes);

		std::string Code;
		std::string CompileError;
		std::vector<unsigned char> Bytecode;
		uint64_t Revision = 0;

		static uint64_t NextSerial();
		const uint64_t Serial = NextSerial();
	};
}
