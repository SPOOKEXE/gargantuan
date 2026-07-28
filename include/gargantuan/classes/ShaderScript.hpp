#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/render/ShaderCompiler.hpp"
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

		// Forces this pass to be treated as producing a different picture every
		// frame. Rarely needed by hand: a pass that reads builtin.Time is
		// detected from its own SPIR-V, so noise, scanlines and tape wobble
		// animate whether or not a script says anything.
		//
		// Left settable for the cases reflection cannot see -- a shader whose
		// animation comes from somewhere the bytecode does not name -- and
		// because it only ever forces the flag on. Turning it off cannot
		// override what the shader was found to read, since that is how a pass
		// freezes.
		//
		// NeedsRedrawEveryFrame is the answer the renderer acts on.
		bool RedrawEveryFrame = false;

		// Whether this pass must run again even on a scene that has not moved:
		// what RedrawEveryFrame was set to, or what the shader turned out to
		// read, whichever says yes.
		//
		// It is the guard on the camera's cascading cache. Everything ahead of
		// the first pass answering true is deterministic in its input, so on a
		// still scene the engine reuses the image it kept from last time and
		// starts work here instead of redrawing the world. A chain with none of
		// these is cached whole, and a still scene costs nothing.
		bool NeedsRedrawEveryFrame();
		// Whether the shader reads builtin.Time, read out of its SPIR-V.
		// Declaring the Builtin block is not enough -- every shader declares
		// the whole of it -- so this is about the members actually loaded.
		bool ReadsBuiltinTime();

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
		// Unique for the run, and never reused. The renderer keys its pipeline
		// cache on this rather than on the address, because a destroyed script
		// frees an address a later one can be handed straight back -- and
		// revisions start again from zero with it, so the two together would
		// name a cache entry belonging to a shader that no longer exists.
		uint64_t GetSerial() const;

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
		// Images the shader can sample alongside the camera's own output. They
		// are bound in the order they were first set, starting at sampler slot
		// 1 because slot 0 is always SourceTexture.
		static constexpr size_t MAXIMUM_IMAGES = 8;

		void SetImage(std::string name, std::shared_ptr<EditableImage> image);
		std::shared_ptr<EditableImage> GetImage(std::string name) const;
		// Binds another camera's rendered output straight from the GPU, with no
		// trip through the CPU. That camera has to have rendered already this
		// frame, which offscreen cameras do before the window one.
		void SetCameraTexture(std::string name, std::shared_ptr<Camera> camera);
		std::shared_ptr<Camera> GetCameraTexture(std::string name) const;
		std::vector<std::string> ListImages();
		void ClearImages();
		// One bound texture, which is either an image or a camera's output
		struct TextureSource {
			std::shared_ptr<EditableImage> Image;
			std::shared_ptr<Camera> Camera;
		};

		// In binding order, for the renderer
		std::vector<std::shared_ptr<EditableImage>> GetImages() const;
		std::vector<TextureSource> GetTextureSources() const;

		// Names the shader actually declares, read out of its SPIR-V. Empty
		// until the shader has been reflected, which Compile and Validate do,
		// and which happens automatically for a named asset.
		std::vector<std::string> GetExpectedParameters();
		// Reads the shader's declared layout. Safe to call repeatedly.
		bool Reflect();
		bool IsReflected() const;
		// True when a parameter of this name can be set
		bool IsParameterExpected(const std::string &name) const;

		std::vector<std::string> ListParameters();
		void ClearParameters();

		// Bound by hand rather than through the generic wrapper, so setting a
		// name the shader never declared can be reported as an error
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

		// Asking costs a file read for a shader that came from an asset, and
		// the renderer asks once a frame, so the answer is kept until the
		// bytecode changes under it. Checked separately from Reflected because
		// a shader with no parameter block at all still has to be answered for.
		bool ReadsTime = false;
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
} // namespace gargantuan
