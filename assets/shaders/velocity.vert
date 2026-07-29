#version 450

// Motion vectors and view depth: where the point under each pixel was on the
// previous frame, and how far away it is on this one. The camera only draws
// this when a pass in its chain asked for Enum.RenderTexture.Velocity or
// .Depth, which in practice means a temporal pass that has to find the same
// surface again in last frame's picture and tell whether it really is the same
// surface.
//
// The depth rides along because it is already here: a perspective projection
// puts the distance from the camera in w, so the fragment stage has it without
// being asked. Drawing the scene a third time to arrive at the same number
// would be the only alternative.
//
// Both positions come from unjittered projections. The sub-pixel offset a
// jittering camera adds is a property of the sampling, not of the scene, and
// letting it into the motion vector would make a still object read as moving
// by whatever the offset changed by between the two frames.

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
