#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

namespace gargantuan {
	class PostProcessShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Fragment;
		}
	};
}
