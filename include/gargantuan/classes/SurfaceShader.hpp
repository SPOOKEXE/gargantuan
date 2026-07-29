#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

namespace gargantuan {
	// Replaces the opaque fragment stage. opaque.vert supplies this fixed interface:
	//   location 0  vec3 FragmentNormal
	//   location 1  vec4 FragmentColor
	//   location 2  vec4 WorldPosition
	//   location 3  vec4 ShadowPosition
	//   set 2, binding 0  sampler2DShadow ShadowMap
	//   set 3, binding 0  WorldUniforms { View, Projection, ShadowBias, LightDirection }
	//   set 3, binding 1  Params { your parameters, bound by name }
	// See assets/shaders/surface_unlit.frag for a minimal implementation.
	class SurfaceShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Fragment;
		}
	};
}
