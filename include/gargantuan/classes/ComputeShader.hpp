#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

#include <glm/glm.hpp>

namespace gargantuan {
	// Dispatches Source over a camera output with this fixed layout:
	//   set 0, binding 0  readonly  image2D SourceTexture   the image so far
	//   set 1, binding 0  writeonly image2D OutputTexture   where to write
	//   set 2, binding 0  Builtin { Resolution, Time }
	//   set 2, binding 1  Params  { your parameters, in Properties:ListParameters order }
	// ThreadGroupSize must equal local_size_x/y/z for SDL pipeline creation.
	class ComputeShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Compute;
		}

		glm::vec3 ThreadGroupSize = glm::vec3(8, 8, 1);
	};
}
