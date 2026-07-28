#include "gargantuan/render/ShaderCompiler.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace gargantuan::ShaderCompiler {
	namespace {
		std::string COMPILER_COMMAND = "glslc";

		const char *StageExtension(Stage stage) {
			switch (stage) {
			case Stage::Vertex:
				return ".vert";
			case Stage::Fragment:
				return ".frag";
			case Stage::Compute:
				return ".comp";
			}
			return ".frag";
		}

		// Shell metacharacters in a path would let a filename run commands, so
		// every path handed to the shell is quoted and any quote is escaped
		std::string QuoteForShell(const std::string &value) {
			std::string quoted = "'";
			for (char character : value) {
				if (character == '\'') {
					quoted += "'\\''";
				} else {
					quoted += character;
				}
			}
			return quoted + "'";
		}

		std::filesystem::path MakeScratchPath(const std::string &name, const char *extension) {
			static std::mt19937_64 generator{std::random_device{}()};
			std::ostringstream unique;
			unique << "gargantuan-" << name << "-" << generator() << extension;
			return std::filesystem::temp_directory_path() / unique.str();
		}
	} // namespace

	std::string GetCompilerCommand() {
		return COMPILER_COMMAND;
	}

	void SetCompilerCommand(std::string command) {
		COMPILER_COMMAND = std::move(command);
	}

	bool IsAvailable() {
		// Probing spawns a process, so remember the answer per command rather
		// than paying for it on every compile
		static std::string probedCommand;
		static bool probedResult = false;

		if (probedCommand == COMPILER_COMMAND) {
			return probedResult;
		}

		std::string probe = COMPILER_COMMAND + " --version > /dev/null 2>&1";
		probedResult = std::system(probe.c_str()) == 0;
		probedCommand = COMPILER_COMMAND;
		return probedResult;
	}

	Result Compile(const std::string &source, Stage stage, const std::string &name) {
		Result result;

		if (source.empty()) {
			result.Error = "Shader source is empty";
			return result;
		}

		if (!IsAvailable()) {
			result.Error = "No GLSL compiler found. Install '" + COMPILER_COMMAND +
						   "' and make sure it is on PATH to compile shader code at runtime.";
			return result;
		}

		const char *extension = StageExtension(stage);
		auto sourcePath = MakeScratchPath(name, extension);
		auto outputPath = MakeScratchPath(name, ".spv");
		auto errorPath = MakeScratchPath(name, ".log");

		{
			std::ofstream file{sourcePath, std::ios::binary};
			if (!file) {
				result.Error = "Could not write a temporary shader file to " + sourcePath.string();
				return result;
			}
			file << source;
		}

		std::string command = COMPILER_COMMAND + " " + QuoteForShell(sourcePath.string()) + " -o " +
							  QuoteForShell(outputPath.string()) + " 2> " + QuoteForShell(errorPath.string());
		int status = std::system(command.c_str());

		// glslc reports errors on stderr with real line numbers, so pass them
		// straight through rather than inventing our own
		std::string diagnostics;
		{
			std::ifstream errorFile{errorPath, std::ios::binary};
			if (errorFile) {
				std::ostringstream buffer;
				buffer << errorFile.rdbuf();
				diagnostics = buffer.str();
			}
		}

		// The compiler names the scratch file it was given; swap that for the
		// shader's own name so the reported errors mean something to a script
		std::string scratchName = sourcePath.string();
		for (size_t at = diagnostics.find(scratchName); at != std::string::npos;
			 at = diagnostics.find(scratchName, at + name.size())) {
			diagnostics.replace(at, scratchName.size(), name);
		}

		if (status == 0) {
			std::ifstream binary{outputPath, std::ios::binary};
			if (binary) {
				result.Bytecode.assign(
					std::istreambuf_iterator<char>(binary), std::istreambuf_iterator<char>()
				);
			}

			if (result.Bytecode.empty()) {
				result.Error = "The compiler produced no output";
			} else {
				result.Success = true;
				// Warnings still matter even when the compile succeeded
				result.Error = diagnostics;
			}
		} else {
			result.Error = diagnostics.empty() ? "The shader compiler failed" : diagnostics;
		}

		std::error_code ignored;
		std::filesystem::remove(sourcePath, ignored);
		std::filesystem::remove(outputPath, ignored);
		std::filesystem::remove(errorPath, ignored);

		return result;
	}

	Result Validate(const std::string &source, Stage stage, const std::string &name) {
		Result result = Compile(source, stage, name);
		result.Bytecode.clear();
		result.Bytecode.shrink_to_fit();
		return result;
	}
} // namespace gargantuan::ShaderCompiler
