#version 450

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec3 VertexNormal;
layout(location = 2) in vec2 VertexUV;

layout(set = 1, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowClipToUvMatrix;
    vec4 LightDirection;
} world;

layout(set = 1, binding = 1) uniform PartUniforms {
    mat4 ModelMatrix;
    vec4 Color;
} part;

layout(location = 0) out vec3 FragmentNormal;
layout(location = 1) out vec4 FragmentColor;
layout(location = 2) out vec4 WorldPosition;
layout(location = 3) out vec4 ShadowPosition;
layout(location = 4) out vec2 FragmentUV;

void main() {
    // Writing stage outputs before gl_Position renders black on affected drivers.
    gl_Position = world.ProjectionMatrix * world.ViewMatrix * part.ModelMatrix * vec4(VertexPosition, 1.0f);

    FragmentNormal = normalize(mat3(part.ModelMatrix) * VertexNormal);
    FragmentColor = part.Color;
    WorldPosition = (part.ModelMatrix * vec4(VertexPosition, 1.0f)).xyzw;
    ShadowPosition = world.ShadowClipToUvMatrix * WorldPosition;
    FragmentUV = VertexUV;
}
