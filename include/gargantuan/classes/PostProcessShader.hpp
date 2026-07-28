#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

namespace gargantuan {
	// Runs Source's fragment shader over the whole of a camera's output. The
	// engine supplies the vertex stage (assets/shaders/fullscreen.vert), so a
	// post-process shader is only ever a .frag.
	//
	// The shader is handed:
	//   set 2, binding 0  sampler2D SourceTexture   the camera's image so far
	//   set 3, binding 0  Builtin { Resolution, Time }
	//   set 3, binding 1  Params  { your parameters, in ListParameters order }
	class PostProcessShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Fragment;
		}
	};
} // namespace gargantuan
