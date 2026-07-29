#version 450

// The opaque vertex stage again, taking its object from a buffer instead of
// from a uniform pushed per part.
//
// Drawing a part at a time means two uniform pushes and a draw call apiece.
// Every one of those is a call into the driver, and at a hundred thousand parts
// that is three hundred thousand calls a frame to say much the same thing each
// time. Here the whole visible set is written into one buffer, and each shape
// is drawn once for however many of it there are: gl_InstanceIndex says which.
//
// Only the parts that need nothing said about them individually come through
// here. A part with a picture on its surface still has per-part fragment
// uniforms and is drawn on its own by the pass.

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec3 VertexNormal;
layout(location = 2) in vec2 VertexUV;

layout(set = 1, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowBiasMatrix;
    vec4 LightDirection;
} world;

struct Instance {
    mat4 ModelMatrix;
    vec4 Color;
    // The face a picture lands on, in world space, and the rule that
    // recognises it. Per part because it depends on how the part is turned,
    // which is the reason a part showing a picture could not be batched before
    // this rode along in the buffer.
    vec4 SurfaceNormal;
    // xy tiles the picture across that face, zw slides it
    vec4 SurfaceTransform;
};

// std430 explicitly: the default packing for a buffer block is not it,
// and the C++ side is writing a plain array of mat4-then-vec4
layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Instance instances[];
};

layout(location = 0) out vec3 FragmentNormal;
layout(location = 1) out vec4 FragmentColor;
layout(location = 2) out vec4 WorldPosition;
layout(location = 3) out vec4 ShadowPosition;
layout(location = 4) out vec2 FragmentUV;
// Flat: they are per instance, so interpolating them across a triangle would
// only blur one constant into itself
layout(location = 5) flat out vec4 SurfaceNormal;
layout(location = 6) flat out vec4 SurfaceTransform;

void main() {
    // NOTE: as in opaque.vert, writing any output before gl_Position renders
    // the whole thing black
    gl_Position = world.ProjectionMatrix * world.ViewMatrix *
        instances[gl_InstanceIndex].ModelMatrix * vec4(VertexPosition, 1.0f);

    mat4 model = instances[gl_InstanceIndex].ModelMatrix;
    FragmentNormal = normalize(mat3(model) * VertexNormal);
    FragmentColor = instances[gl_InstanceIndex].Color;
    WorldPosition = (model * vec4(VertexPosition, 1.0f)).xyzw;
    ShadowPosition = world.ShadowBiasMatrix * WorldPosition;
    FragmentUV = VertexUV;
    SurfaceNormal = instances[gl_InstanceIndex].SurfaceNormal;
    SurfaceTransform = instances[gl_InstanceIndex].SurfaceTransform;
}
