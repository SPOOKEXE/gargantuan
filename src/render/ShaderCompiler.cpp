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
		// every path handed to the shell is quoted and any quote escaped.
		// cmd.exe has no single-quoting and its own escaping rules, hence the
		// split.
		std::string QuoteForShell(const std::string &value) {
#ifdef _WIN32
			std::string quoted = "\"";
			for (char character : value) {
				// cmd.exe cannot quote these at all; a path containing one is
				// refused rather than passed through
				if (character == '"' || character == '%' || character == '\n') {
					continue;
				}
				quoted += character;
			}
			return quoted + "\"";
#else
			std::string quoted = "'";
			for (char character : value) {
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

		// Where compiled runtime shaders are kept between runs
		std::filesystem::path CacheDirectory() {
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

		// FNV-1a over the source and stage. Two different shaders colliding
		// would serve the wrong bytecode, so the stage is folded in too.
		std::string CacheKey(const std::string &source, Stage stage) {
			uint64_t hash = 1469598103934665603ull;
			auto mix = [&hash](unsigned char byte) {
				hash ^= byte;
				hash *= 1099511628211ull;
			};

			for (char character : source) {
				mix((unsigned char)character);
			}
			mix((unsigned char)stage);

			char text[32];
			std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)hash);
			return std::string(text) + StageExtension(stage) + ".spv";
		}

		bool CACHE_ENABLED = true;

		std::filesystem::path MakeScratchPath(const std::string &name, const char *extension) {
			static std::mt19937_64 generator{std::random_device{}()};
			std::ostringstream unique;
			unique << "gargantuan-" << name << "-" << generator() << extension;
			return std::filesystem::temp_directory_path() / unique.str();
		}
	} // namespace

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

		// Compiles in-process, so nothing has to be installed alongside the game
		Result CompileInProcess(const std::string &source, Stage stage, const std::string &name) {
			Result result;

			shaderc_compiler_t compiler = shaderc_compiler_initialize();
			if (!compiler) {
				result.Error = "Could not start the shader compiler";
				return result;
			}

			shaderc_compile_options_t options = shaderc_compile_options_initialize();
			shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);

			shaderc_compilation_result_t compiled = shaderc_compile_into_spv(
				compiler, source.c_str(), source.size(), ShaderKind(stage), name.c_str(), "main", options
			);

			if (const char *message = shaderc_result_get_error_message(compiled)) {
				result.Error = message;
			}

			if (shaderc_result_get_compilation_status(compiled) == shaderc_compilation_status_success) {
				const char *bytes = shaderc_result_get_bytes(compiled);
				size_t length = shaderc_result_get_length(compiled);
				result.Bytecode.assign(bytes, bytes + length);
				result.Success = !result.Bytecode.empty();
				if (!result.Success) {
					result.Error = "The compiler produced no output";
				}
			}

			shaderc_result_release(compiled);
			shaderc_compile_options_release(options);
			shaderc_compiler_release(compiler);
			return result;
		}
	} // namespace
#endif

	bool IsInProcess() {
#ifdef GARGANTUAN_HAVE_SHADERC
		return true;
#else
		return false;
#endif
	}

	namespace {
		void WriteToCache(const Result &result, const std::filesystem::path &path) {
			if (!CACHE_ENABLED || !result.Success || result.Bytecode.empty()) {
				return;
			}

			std::error_code ignored;
			std::filesystem::create_directories(path.parent_path(), ignored);

			std::ofstream file{path, std::ios::binary};
			if (file) {
				file.write((const char *)result.Bytecode.data(), (std::streamsize)result.Bytecode.size());
			}
		}
	} // namespace

	void SetCacheEnabled(bool enabled) {
		CACHE_ENABLED = enabled;
	}

	std::string GetCacheDirectory() {
		return CacheDirectory().string();
	}

	std::string GetCompilerCommand() {
		return COMPILER_COMMAND;
	}

	void SetCompilerCommand(std::string command) {
		COMPILER_COMMAND = std::move(command);
	}

	bool IsAvailable() {
#ifdef GARGANTUAN_HAVE_SHADERC
		// Compiled in, so nothing external is needed
		return true;
#else
		// Probing spawns a process, so remember the answer per command rather
		// than paying for it on every compile
		static std::string probedCommand;
		static bool probedResult = false;

		if (probedCommand == COMPILER_COMMAND) {
			return probedResult;
		}

		std::string probe = COMPILER_COMMAND + " --version > " + NullDevice() + " 2>&1";
		probedResult = std::system(probe.c_str()) == 0;
		probedCommand = COMPILER_COMMAND;
		return probedResult;
#endif
	}

	Result Compile(const std::string &source, Stage stage, const std::string &name) {
		Result result;

		if (source.empty()) {
			result.Error = "Shader source is empty";
			return result;
		}

		// A shader that has been compiled before does not need compiling again
		auto cachePath = CacheDirectory() / CacheKey(source, stage);
		if (CACHE_ENABLED) {
			std::ifstream cached{cachePath, std::ios::binary};
			if (cached) {
				result.Bytecode.assign(
					std::istreambuf_iterator<char>(cached), std::istreambuf_iterator<char>()
				);
				if (!result.Bytecode.empty()) {
					result.Success = true;
					result.FromCache = true;
					return result;
				}
			}
		}

#ifdef GARGANTUAN_HAVE_SHADERC
		{
			Result compiled = CompileInProcess(source, stage, name);
			WriteToCache(compiled, cachePath);
			return compiled;
		}
#else

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

		WriteToCache(result, cachePath);
		return result;
#endif
	}

	Result Validate(const std::string &source, Stage stage, const std::string &name) {
		Result result = Compile(source, stage, name);
		result.Bytecode.clear();
		result.Bytecode.shrink_to_fit();
		return result;
	}
} // namespace gargantuan::ShaderCompiler
