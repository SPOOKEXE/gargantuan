#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

namespace gargantuan {
	// Runs a .frag Source over the camera output using fullscreen.vert and this layout:
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
}
