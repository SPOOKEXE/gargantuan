#version 450

// Where the point under each pixel was last frame, and how far away it is now.
// Only drawn when a pass asked for one of them.
//
// The depth rides along because a perspective projection already puts the
// distance in w. Both positions are unjittered: the sub-pixel offset belongs to
// the sampling, not the scene, and would make a still object read as moving.

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
