#version 450

// Positions exclude projection jitter.

layout(location = 0) in vec3 VertexPosition;

layout(set = 1, binding = 0) uniform WorldUniforms {
    mat4 ViewProjection;
    mat4 PreviousViewProjection;
} world;

layout(set = 1, binding = 1) uniform PartUniforms {
    mat4 ModelMatrix;
    mat4 PreviousModelMatrix;
} part;

layout(location = 0) out vec4 CurrentClip;
layout(location = 1) out vec4 PreviousClip;

void main() {
    vec4 position = vec4(VertexPosition, 1.0);

    // Writing stage outputs before gl_Position renders black on affected drivers.
    gl_Position = world.ViewProjection * part.ModelMatrix * position;

    CurrentClip = gl_Position;
    PreviousClip = world.PreviousViewProjection * part.PreviousModelMatrix * position;
}
