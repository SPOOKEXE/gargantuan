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
	};

	// True when shaderc was linked in, so compilation happens inside the
	// process and needs nothing installed next to the game
	bool IsInProcess();

	// True when a GLSL compiler can be found, ie. whether Compile can work at
	// all on this machine
	bool IsAvailable();
	// Path or name of the compiler being used
	std::string GetCompilerCommand();
	// Overrides the compiler, mainly so tests can point at a known binary
	void SetCompilerCommand(std::string command);

	// Compiles GLSL to SPIR-V by shelling out. `name` only decorates the
	// diagnostics. SDL's GPU API takes bytecode and never GLSL, so runtime
	// shader code has to go through a compiler like this one.
	Result Compile(const std::string &source, Stage stage, const std::string &name = "shader");

	// Compiles and throws the bytecode away, for checking a script's shader
	// without building a pipeline from it
	Result Validate(const std::string &source, Stage stage, const std::string &name = "shader");
} // namespace gargantuan::ShaderCompiler
