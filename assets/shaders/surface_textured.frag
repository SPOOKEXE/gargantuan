#version 450

layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;

layout(location = 0) out vec4 OutputColor;

// Binding 0 is always the engine shadow map.
layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;
layout(set = 2, binding = 1) uniform sampler2D Skin;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowClipToUvMatrix;
    vec4 LightDirection;
} world;

layout(set = 3, binding = 1) uniform Params {
    vec4 Scale;
} params;

void main() {
    vec3 normal = abs(normalize(FragmentNormal));
    vec2 uv;
    if (normal.y > normal.x && normal.y > normal.z) {
        uv = WorldPosition.xz;
    } else if (normal.x > normal.z) {
        uv = WorldPosition.zy;
    } else {
        uv = WorldPosition.xy;
    }

    vec4 skin = texture(Skin, uv * max(params.Scale.x, 0.0001));
    float lighting = 0.2 + max(dot(normalize(FragmentNormal), normalize(world.LightDirection.xyz)), 0.0);
    OutputColor = vec4(FragmentColor.rgb * skin.rgb * lighting, FragmentColor.a);
}
