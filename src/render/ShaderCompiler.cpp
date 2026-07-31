#include "gargantuan/render/ShaderCompiler.hpp"

#ifdef GARGANTUAN_HAVE_SHADERC
#include <shaderc/shaderc.h>
#endif

#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace gargantuan::ShaderCompiler {
	namespace {
		const std::string COMPILER_COMMAND = "glslc";

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

		// Quote untrusted paths with platform-specific shell rules.
		std::string QuoteForShell(const std::string &path) {
#ifdef _WIN32
			std::string quoted = "\"";
			for (char character : path) {
				// Drop characters this cmd.exe quoting scheme cannot represent safely.
				if (character == '"' || character == '%' || character == '\n') {
					continue;
				}
				quoted += character;
			}
			return quoted + "\"";
#else
			std::string quoted = "'";
			for (char character : path) {
				if (character == '\'') {
					quoted += "'\\''";
				} else {
					quoted += character;
				}
			}
			return quoted + "'";
#endif
		}

		const char *NullDevice() {
#ifdef _WIN32
			return "NUL";
#else
			return "/dev/null";
#endif
		}

		// SPIR-V on disk, addressed by the hash of the source that produced it.
		// Compiling is the slow part and the answer never changes, so this is
		// the only reason a second run starts faster than the first.
		namespace BytecodeCache {
			std::filesystem::path Directory() {
				static std::filesystem::path directory = [] {
					char *preferences = SDL_GetPrefPath("TeamFireworks", "Gargantuan");
					std::filesystem::path base = preferences ? std::filesystem::path(preferences)
															 : std::filesystem::temp_directory_path();
					if (preferences) {
						SDL_free(preferences);
					}
					return base / "shadercache";
				}();
				return directory;
			}

			// FNV-1a over the GLSL code and stage, so a changed shader is a changed name
			// and a stale entry can never be found rather than needing eviction.
			std::filesystem::path PathFor(const std::string &glslCode, Stage stage) {
				uint64_t hash = 1469598103934665603ull;
				auto mix = [&hash](unsigned char byte) {
					hash ^= byte;
					hash *= 1099511628211ull;
				};

				for (char character : glslCode) {
					mix((unsigned char)character);
				}
				mix((unsigned char)stage);

				char hashHex[32];
				std::snprintf(hashHex, sizeof(hashHex), "%016llx", (unsigned long long)hash);
				return Directory() / (std::string(hashHex) + StageExtension(stage) + ".spv");
			}

			// Empty means no entry; an entry that exists is always current.
			std::vector<unsigned char> ReadCachedBytecode(const std::filesystem::path &path) {
				std::ifstream file{path, std::ios::binary};
				if (!file) {
					return {};
				}
				return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
			}

			void Write(const CompileResult &result, const std::filesystem::path &path) {
				if (!result.IsSuccess || result.SPIRVBytecode.empty()) {
					return;
				}

				std::error_code ignored;
				std::filesystem::create_directories(path.parent_path(), ignored);

				std::ofstream file{path, std::ios::binary};
				if (file) {
					file.write((const char *)result.SPIRVBytecode.data(), (std::streamsize)result.SPIRVBytecode.size());
				}
			}
		}

		std::filesystem::path MakeScratchPath(const std::string &diagnosticName, const char *extension) {
			static std::mt19937_64 generator{std::random_device{}()};
			std::ostringstream unique;
			unique << "gargantuan-" << diagnosticName << "-" << generator() << extension;
			return std::filesystem::temp_directory_path() / unique.str();
		}
	}

#ifdef GARGANTUAN_HAVE_SHADERC
	namespace {
		shaderc_shader_kind ShaderKind(Stage stage) {
			switch (stage) {
			case Stage::Vertex:
				return shaderc_glsl_vertex_shader;
			case Stage::Compute:
				return shaderc_glsl_compute_shader;
			case Stage::Fragment:
			default:
				return shaderc_glsl_fragment_shader;
			}
		}

		CompileResult CompileInProcess(const std::string &glslCode, Stage stage, const std::string &diagnosticName) {
			CompileResult result;

			shaderc_compiler_t compiler = shaderc_compiler_initialize();
			if (!compiler) {
				result.Diagnostics = "Could not start the shader compiler";
				return result;
			}

			shaderc_compile_options_t options = shaderc_compile_options_initialize();
			// Unoptimised, deliberately, and it has to stay that way. The
			// optimiser strips OpName and OpMemberName, and ShaderReflection keys
			// uniform block members by their declared name -- so at
			// `_performance` a block reflects as WasBlockFound = false with no members,
			// RegisterShaderNode registers no parameters, and ReflectBlockUsage
			// reports nothing read so a changed parameter never invalidates the
			// cache. All of that fails silently: the shader compiles and draws,
			// it just ignores every parameter it was given.
			//
			// This is also why the subprocess path passes no -O, and why the
			// build-time glslc in CMakeLists.txt does not either. The three have
			// to agree, because which one built a shader is not supposed to
			// change what the shader does.
			shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_zero);

			shaderc_compilation_result_t compiled = shaderc_compile_into_spv(
				compiler, glslCode.c_str(), glslCode.size(), ShaderKind(stage), diagnosticName.c_str(), "main", options
			);

			if (const char *message = shaderc_result_get_error_message(compiled)) {
				result.Diagnostics = message;
			}

			if (shaderc_result_get_compilation_status(compiled) == shaderc_compilation_status_success) {
				const char *bytes = shaderc_result_get_bytes(compiled);
				size_t length = shaderc_result_get_length(compiled);
				result.SPIRVBytecode.assign(bytes, bytes + length);
				result.IsSuccess = !result.SPIRVBytecode.empty();
				if (!result.IsSuccess) {
					result.Diagnostics = "The compiler produced no output";
				}
			}

			shaderc_result_release(compiled);
			shaderc_compile_options_release(options);
			shaderc_compiler_release(compiler);
			return result;
		}
	}
#endif

	bool IsAvailable() {
#ifdef GARGANTUAN_HAVE_SHADERC
		return true;
#else
		// Probing spawns a process, and the answer cannot change within a run.
		static const bool wasCompilerFound = [] {
			std::string probeCommand = COMPILER_COMMAND + " --version > " + NullDevice() + " 2>&1";
			return std::system(probeCommand.c_str()) == 0;
		}();
		return wasCompilerFound;
#endif
	}

	CompileResult Compile(const std::string &glslCode, Stage stage, const std::string &diagnosticName) {
		CompileResult result;

		if (glslCode.empty()) {
			result.Diagnostics = "Shader source is empty";
			return result;
		}

		auto cachePath = BytecodeCache::PathFor(glslCode, stage);
		result.SPIRVBytecode = BytecodeCache::ReadCachedBytecode(cachePath);
		if (!result.SPIRVBytecode.empty()) {
			result.IsSuccess = true;
			result.WasServedFromCache = true;
			return result;
		}

#ifdef GARGANTUAN_HAVE_SHADERC
		{
			CompileResult compiled = CompileInProcess(glslCode, stage, diagnosticName);
			BytecodeCache::Write(compiled, cachePath);
			return compiled;
		}
#else

		if (!IsAvailable()) {
			result.Diagnostics = "No GLSL compiler found. Install '" + COMPILER_COMMAND +
						   "' and make sure it is on PATH to compile shader code at runtime.";
			return result;
		}

		const char *extension = StageExtension(stage);
		auto sourcePath = MakeScratchPath(diagnosticName, extension);
		auto outputPath = MakeScratchPath(diagnosticName, ".spv");
		auto errorPath = MakeScratchPath(diagnosticName, ".log");

		{
			std::ofstream file{sourcePath, std::ios::binary};
			if (!file) {
				result.Diagnostics = "Could not write a temporary shader file to " + sourcePath.string();
				return result;
			}
			file << glslCode;
		}

		std::string command = COMPILER_COMMAND + " " + QuoteForShell(sourcePath.string()) + " -o " +
							  QuoteForShell(outputPath.string()) + " 2> " + QuoteForShell(errorPath.string());
		int exitCode = std::system(command.c_str());

		// Preserve glslc stderr and source line numbers.
		std::string diagnostics;
		{
			std::ifstream errorFile{errorPath, std::ios::binary};
			if (errorFile) {
				std::ostringstream buffer;
				buffer << errorFile.rdbuf();
				diagnostics = buffer.str();
			}
		}

		// Replace scratch paths with the shader name in diagnostics.
		std::string scratchSourcePath = sourcePath.string();
		for (size_t at = diagnostics.find(scratchSourcePath); at != std::string::npos;
			 at = diagnostics.find(scratchSourcePath, at + diagnosticName.size())) {
			diagnostics.replace(at, scratchSourcePath.size(), diagnosticName);
		}

		if (exitCode == 0) {
			std::ifstream binary{outputPath, std::ios::binary};
			if (binary) {
				result.SPIRVBytecode.assign(
					std::istreambuf_iterator<char>(binary), std::istreambuf_iterator<char>()
				);
			}

			if (result.SPIRVBytecode.empty()) {
				result.Diagnostics = "The compiler produced no output";
			} else {
				result.IsSuccess = true;
				result.Diagnostics = diagnostics;
			}
		} else {
			result.Diagnostics = diagnostics.empty() ? "The shader compiler failed" : diagnostics;
		}

		std::error_code ignored;
		std::filesystem::remove(sourcePath, ignored);
		std::filesystem::remove(outputPath, ignored);
		std::filesystem::remove(errorPath, ignored);

		BytecodeCache::Write(result, cachePath);
		return result;
#endif
	}

	CompileResult CompileAndDiscardBytecode(const std::string &glslCode, Stage stage, const std::string &diagnosticName) {
		CompileResult result = Compile(glslCode, stage, diagnosticName);
		result.SPIRVBytecode.clear();
		result.SPIRVBytecode.shrink_to_fit();
		return result;
	}
}
