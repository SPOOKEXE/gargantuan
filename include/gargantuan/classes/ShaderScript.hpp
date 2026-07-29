#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/render/ShaderCompiler.hpp"
#include "gargantuan/render/ShaderPresets.hpp"
#include "gargantuan/render/ShaderReflection.hpp"

#include <cstdint>
#include <lua.h>
#include <glm/glm.hpp>
#include <memory>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	class Camera;

	// Camera shader base. Source names a build-time asset without its extension.
	// Runtime GLSL must be compiled to bytecode before SDL can use it.
	class ShaderScript : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// Maximum parameter slots in one uniform buffer.
		static constexpr size_t MAXIMUM_PARAMETERS = 64;

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

		// Parameters use 16-byte std140 slots in first-set order.
		void SetNumber(std::string name, float value);
		void SetVector2(std::string name, Vector2 value);
		void SetVector3(std::string name, glm::vec3 value);
		void SetColor3(std::string name, Color3 value);
		void SetBool(std::string name, bool value);
		// Images bind in first-set order after SourceTexture at sampler slot 0.
		static constexpr size_t MAXIMUM_IMAGES = 8;

		void SetImage(std::string name, std::shared_ptr<EditableImage> image);
		std::shared_ptr<EditableImage> GetImage(std::string name) const;
		// Binds another camera's GPU output; that camera must render first.
		void SetCameraTexture(std::string name, std::shared_ptr<Camera> camera);
		std::shared_ptr<Camera> GetCameraTexture(std::string name) const;
		// Binds a buffer owned by the camera running the pass; requesting it enables production.
		void SetRenderTexture(std::string name, Enums::RenderTexture texture);
		Enums::RenderTexture GetRenderTexture(std::string name) const;
		std::vector<std::string> ListImages();
		void ClearImages();
		// One bound texture: an image, another camera's output, or one of the
		// reader camera's own buffers
		struct TextureSource {
			std::shared_ptr<EditableImage> Image;
			std::shared_ptr<Camera> Camera;
			Enums::RenderTexture Render = Enums::RenderTexture::None;
		};

		// In binding order, for the renderer
		std::vector<std::shared_ptr<EditableImage>> GetImages() const;
		std::vector<TextureSource> GetTextureSources() const;

		// SPIR-V names. Compile/Validate reflect runtime code; assets reflect lazily.
		std::vector<std::string> GetExpectedParameters();
		// Reads the shader's declared layout. Safe to call repeatedly.
		bool Reflect();
		bool IsReflected() const;
		// True when a parameter of this name can be set
		bool IsParameterExpected(const std::string &name) const;

		std::vector<std::string> ListParameters();
		void ClearParameters();

		// Manual bindings validate names against reflection.
		static int LSetNumber(lua_State *L, Instance *instance);
		static int LSetVector2(lua_State *L, Instance *instance);
		static int LSetVector3(lua_State *L, Instance *instance);
		static int LSetColor3(lua_State *L, Instance *instance);
		static int LSetBool(lua_State *L, Instance *instance);
		// Bound by hand too, so a binding's name can be an Enum.ShaderProperty
		static int LSetImage(lua_State *L, Instance *instance);
		static int LGetImage(lua_State *L, Instance *instance);
		static int LSetCameraTexture(lua_State *L, Instance *instance);
		static int LGetCameraTexture(lua_State *L, Instance *instance);
		static int LSetRenderTexture(lua_State *L, Instance *instance);
		static int LGetRenderTexture(lua_State *L, Instance *instance);

		// Packed slots, ready to push as uniform data. Only used when a shader's
		// layout could not be reflected.
		const std::vector<glm::vec4> &GetPackedParameters() const;
		size_t GetPackedParameterBytes() const;

		// Name and value of every parameter, in the order they were first set
		std::vector<std::pair<std::string, glm::vec4>> GetParameters() const;

	  protected:
		void SetParameter(const std::string &name, glm::vec4 value);

	  private:
		std::vector<std::string> ParameterOrder;
		std::unordered_map<std::string, size_t> ParameterIndices;
		std::vector<glm::vec4> ParameterValues;

		ShaderReflection::BlockLayout DeclaredParameters;
		bool Reflected = false;

		// Cached until bytecode changes; independent of parameter-block reflection.
		bool ReadsTime = false;
		bool ReadsJitter = false;
		bool BuiltinsChecked = false;
		// Reads the builtin block's usage out of `spirv`, and records that the
		// question has now been asked
		void CheckBuiltins(const void *spirv, size_t bytes);

		std::vector<std::string> ImageOrder;
		std::unordered_map<std::string, TextureSource> Images;
		std::string Code;
		std::string CompileError;
		std::vector<unsigned char> Bytecode;
		uint64_t Revision = 0;

		static uint64_t NextSerial();
		const uint64_t Serial = NextSerial();
	};
}
