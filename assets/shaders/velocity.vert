#version 450

// Emits prior position and current depth on demand. Positions exclude jitter.

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

    // NOTE: as in opaque.vert, writing any output before gl_Position renders
    // the whole thing black
    gl_Position = world.ViewProjection * part.ModelMatrix * position;

    CurrentClip = gl_Position;
    PreviousClip = world.PreviousViewProjection * part.PreviousModelMatrix * position;
}
