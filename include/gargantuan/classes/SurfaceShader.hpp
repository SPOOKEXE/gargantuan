#pragma once

#include "gargantuan/classes/ShaderScript.hpp"

namespace gargantuan {
	// Replaces the fragment stage the opaque pass normally uses, so a camera
	// can decide how every object it draws is shaded. Unlike a
	// PostProcessShader, which sees only the finished picture, this runs per
	// fragment and has the surface's own data to work with.
	//
	// The engine supplies the vertex stage (assets/shaders/opaque.vert), so a
	// surface shader is only ever a .frag, and it must declare exactly what
	// that vertex stage feeds it:
	//
	//   location 0  vec3 FragmentNormal
	//   location 1  vec4 FragmentColor
	//   location 2  vec4 WorldPosition
	//   location 3  vec4 ShadowPosition
	//
	//   set 2, binding 0  sampler2DShadow ShadowMap
	//   set 3, binding 0  WorldUniforms { View, Projection, ShadowBias, LightDirection }
	//   set 3, binding 1  Params { your parameters, bound by name }
	//
	// See assets/shaders/surface_unlit.frag for the smallest working one.
	class SurfaceShader : public ShaderScript {
	  public:
		static const ClassDefinition DEFINITION;

		ShaderCompiler::Stage GetStage() const override {
			return ShaderCompiler::Stage::Fragment;
		}
	};
} // namespace gargantuan
