#version 450

// Preset surface shader: draws every object in flat colour, ignoring the sun
// and the shadow map entirely.
//
//   local unlit = Instance.new("SurfaceShader")
//   unlit.Source = "surface_unlit"
//   unlit:SetNumber("Brightness", 1)
//   camera.SurfaceShader = unlit
//
// Every input below must be declared even when unused, because the vertex
// stage and the pipeline both expect them.

layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;

layout(location = 0) out vec4 OutputColor;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowBiasMatrix;
    vec4 LightDirection;
} world;

layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;

layout(set = 3, binding = 1) uniform Params {
    vec4 Brightness;
} params;

void main() {
    OutputColor = vec4(FragmentColor.rgb * params.Brightness.x, FragmentColor.a);
}
