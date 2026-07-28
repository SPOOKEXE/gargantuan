#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/render/ShaderCompiler.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	// Base for the shaders a Camera can run over its output.
	//
	// Source names a shader that glslc compiled at build time from
	// assets/shaders, without the extension: "vignette" finds vignette.frag
	// for a PostProcessShader and vignette.comp for a ComputeShader. The engine
	// does not compile GLSL at runtime -- SDL's GPU API only accepts bytecode.
	class ShaderScript : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// One uniform buffer's worth of parameters is plenty, and keeping a
		// bound means a bad script cannot grow it without limit
		static constexpr size_t MAXIMUM_PARAMETERS = 64;

		// Shader asset name, no extension and no directory. Ignored when Code
		// has been set and compiled.
		std::string Source;

		// GLSL source compiled at runtime. Setting it marks the script dirty;
		// Compile() then turns it into bytecode, or fills CompileError.
		std::string GetCode() const;
		void SetCode(std::string code);

		// Compiles Code, returning whether it worked. The diagnostics land in
		// CompileError either way, so warnings survive a success.
		bool Compile();
		// Compiles without keeping the result, for checking a shader before
		// handing it to a camera
		bool Validate();
		std::string GetCompileError() const;

		// True once Code has compiled and the bytecode is ready to use
		bool HasBytecode() const;
		const std::vector<unsigned char> &GetBytecode() const;
		// Bumped every time the bytecode changes, so the renderer knows when to
		// rebuild the pipeline it cached
		uint64_t GetRevision() const;

		// Which stage this kind of shader compiles as
		virtual ShaderCompiler::Stage GetStage() const = 0;

		// Each parameter occupies one 16-byte slot, which is what std140 wants
		// for a vec4 anyway. Slots are ordered by when the parameter was first
		// set, so a shader's uniform block members must be declared in that
		// same order; ListParameters reports it.
		void SetNumber(std::string name, float value);
		void SetVector2(std::string name, Vector2 value);
		void SetVector3(std::string name, glm::vec3 value);
		void SetColor3(std::string name, Color3 value);
		void SetBool(std::string name, bool value);
		// One image the shader can sample alongside the camera's own output.
		// Bound as sampler slot 1, after SourceTexture.
		void SetImage(std::shared_ptr<EditableImage> image);
		std::shared_ptr<EditableImage> GetImage() const;

		std::vector<std::string> ListParameters();
		void ClearParameters();

		// Packed slots, ready to push as uniform data
		const std::vector<glm::vec4> &GetPackedParameters() const;
		size_t GetPackedParameterBytes() const;

	  protected:
		void SetParameter(const std::string &name, glm::vec4 value);

	  private:
		std::vector<std::string> ParameterOrder;
		std::unordered_map<std::string, size_t> ParameterIndices;
		std::vector<glm::vec4> ParameterValues;

		std::shared_ptr<EditableImage> Image;
		std::string Code;
		std::string CompileError;
		std::vector<unsigned char> Bytecode;
		uint64_t Revision = 0;
	};
} // namespace gargantuan
