#version 450

// Inputs must match the vertex-stage pipeline contract, even when unused.

layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;

layout(location = 0) out vec4 OutputColor;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowClipToUvMatrix;
    vec4 LightDirection;
} world;

layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;

layout(set = 3, binding = 1) uniform Params {
    vec4 Brightness;
} params;

void main() {
    OutputColor = vec4(FragmentColor.rgb * params.Brightness.x, FragmentColor.a);
}
