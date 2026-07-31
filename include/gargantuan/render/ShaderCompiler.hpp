#pragma once

#include <string>
#include <vector>

namespace gargantuan::ShaderCompiler {
	enum class Stage {
		Vertex,
		Fragment,
		Compute,
	};

	struct CompileResult {
		bool IsSuccess = false;
		std::vector<unsigned char> SPIRVBytecode;
		std::string Diagnostics;
		bool WasServedFromCache = false;
	};

	bool IsAvailable();

	CompileResult Compile(const std::string &glslCode, Stage stage, const std::string &diagnosticName = "shader");

	CompileResult
	CompileAndDiscardBytecode(const std::string &glslCode, Stage stage, const std::string &diagnosticName = "shader");
}
