#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

#include <glm/glm.hpp>

namespace gargantuan {
	class ComputeShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Compute;
		}

		glm::vec3 ThreadGroupSize = glm::vec3(8, 8, 1);
	};
}
