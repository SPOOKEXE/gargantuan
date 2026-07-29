#pragma once

#include <string>
#include <vector>

namespace gargantuan::ShaderCompiler {
	enum class Stage {
		Vertex,
		Fragment,
		Compute,
	};

	struct Result {
		bool Success = false;
		// SPIR-V, empty when the compile failed
		std::vector<unsigned char> Bytecode;
		// glslc's own diagnostics, so line numbers and messages are the real ones
		std::string Error;
		// True when the bytecode came off disk rather than being compiled now
		bool FromCache = false;
	};

	// Disk cache is keyed by source and stage.
	void SetCacheEnabled(bool enabled);
	std::string GetCacheDirectory();

	// True when shaderc provides in-process compilation.
	bool IsInProcess();

	// True when Compile has an available compiler backend.
	bool IsAvailable();
	std::string GetCompilerCommand();
	// Overrides the compiler, mainly so tests can point at a known binary
	void SetCompilerCommand(std::string command);

	// Compiles GLSL to SPIR-V; `name` only labels diagnostics.
	Result Compile(const std::string &source, Stage stage, const std::string &name = "shader");

	// Compiles and throws the bytecode away, for checking a script's shader
	// without building a pipeline from it
	Result Validate(const std::string &source, Stage stage, const std::string &name = "shader");
}
