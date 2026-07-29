#version 450

// Opaque instanced path: gl_InstanceIndex selects per-object buffer data.
// Objects needing per-part fragment uniforms remain on the non-instanced path.

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
    // The top three rows of the model matrix, transposed. The bottom row is
    // always (0,0,0,1), so it is rebuilt here rather than uploaded.
    vec4 ModelRows[3];
    vec4 Color;
    // World-space textured face and match rule, per instance.
    vec4 SurfaceNormal;
    // xy tiles the picture across that face, zw slides it
    vec4 SurfaceTransform;
};

// Must match the C++ InstanceData: three matrix columns then three vec4s.
layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Instance instances[];
};

layout(location = 0) out vec3 FragmentNormal;
layout(location = 1) out vec4 FragmentColor;
layout(location = 2) out vec4 WorldPosition;
layout(location = 3) out vec4 ShadowPosition;
layout(location = 4) out vec2 FragmentUV;
// Per-instance values must not interpolate.
layout(location = 5) flat out vec4 SurfaceNormal;
layout(location = 6) flat out vec4 SurfaceTransform;

void main() {
    // NOTE: as in opaque.vert, writing any output before gl_Position renders
    // the whole thing black
    // mat4() takes columns, so feeding it rows builds the transpose.
    mat4 model = transpose(mat4(
        instances[gl_InstanceIndex].ModelRows[0],
        instances[gl_InstanceIndex].ModelRows[1],
        instances[gl_InstanceIndex].ModelRows[2],
        vec4(0.0f, 0.0f, 0.0f, 1.0f)
    ));

    gl_Position = world.ProjectionMatrix * world.ViewMatrix * model * vec4(VertexPosition, 1.0f);
    FragmentNormal = normalize(mat3(model) * VertexNormal);
    FragmentColor = instances[gl_InstanceIndex].Color;
    WorldPosition = (model * vec4(VertexPosition, 1.0f)).xyzw;
    ShadowPosition = world.ShadowBiasMatrix * WorldPosition;
    FragmentUV = VertexUV;
    SurfaceNormal = instances[gl_InstanceIndex].SurfaceNormal;
    SurfaceTransform = instances[gl_InstanceIndex].SurfaceTransform;
}
