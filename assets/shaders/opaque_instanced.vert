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

struct Instance {
    // The top three rows of the model matrix, transposed. The bottom row is
    // always (0,0,0,1), so it is rebuilt here rather than uploaded.
    vec4 ModelRows[3];
    vec4 Color;
    vec4 SurfaceNormalAndRule;
    vec4 SurfaceTilingOffset;
};

// Must match the C++ InstanceData: three matrix columns then three vec4s.
layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Instance instances[];
};

// Visible registry-row indices in batch order; gl_InstanceIndex includes first_instance.
layout(std430, set = 0, binding = 1) readonly buffer VisibleIndices {
    uint visibleIndices[];
};

layout(location = 0) out vec3 FragmentNormal;
layout(location = 1) out vec4 FragmentColor;
layout(location = 2) out vec4 WorldPosition;
layout(location = 3) out vec4 ShadowPosition;
layout(location = 4) out vec2 FragmentUV;
layout(location = 5) flat out vec4 SurfaceNormalAndRule;
layout(location = 6) flat out vec4 SurfaceTilingOffset;

void main() {
    uint slot = visibleIndices[gl_InstanceIndex];

    // Writing stage outputs before gl_Position renders black on affected drivers.
    mat4 model = transpose(mat4(
        instances[slot].ModelRows[0],
        instances[slot].ModelRows[1],
        instances[slot].ModelRows[2],
        vec4(0.0f, 0.0f, 0.0f, 1.0f)
    ));

    gl_Position = world.ProjectionMatrix * world.ViewMatrix * model * vec4(VertexPosition, 1.0f);
    FragmentNormal = normalize(mat3(model) * VertexNormal);
    FragmentColor = instances[slot].Color;
    WorldPosition = (model * vec4(VertexPosition, 1.0f)).xyzw;
    ShadowPosition = world.ShadowClipToUvMatrix * WorldPosition;
    FragmentUV = VertexUV;
    SurfaceNormalAndRule = instances[slot].SurfaceNormalAndRule;
    SurfaceTilingOffset = instances[slot].SurfaceTilingOffset;
}
