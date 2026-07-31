#version 450

// Position-only stage; the capability pass must not require normal or UV streams.
layout(location = 0) in vec3 VertexPosition;

layout(set = 1, binding = 0) uniform Uniforms {
    mat4 ShadowMatrix;
} uniforms;

struct Instance {
    // The top three rows of the model matrix, transposed. The bottom row is
    // always (0,0,0,1), so it is rebuilt here rather than uploaded.
    vec4 ModelRows[3];
    vec4 Color;
    vec4 SurfaceNormalAndRule;
    vec4 SurfaceTilingOffset;
};

// Must match C++ InstanceData and opaque_instanced.vert's binding.
layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Instance instances[];
} instanceBuffer;

// Caster registry-row indices; this set differs from the opaque visible set.
layout(std430, set = 0, binding = 1) readonly buffer VisibleIndices {
    uint visibleIndices[];
};

void main() {
    Instance self = instanceBuffer.instances[visibleIndices[gl_InstanceIndex]];

    mat4 model = mat4(
            vec4(self.ModelRows[0].x, self.ModelRows[1].x, self.ModelRows[2].x, 0.0),
            vec4(self.ModelRows[0].y, self.ModelRows[1].y, self.ModelRows[2].y, 0.0),
            vec4(self.ModelRows[0].z, self.ModelRows[1].z, self.ModelRows[2].z, 0.0),
            vec4(self.ModelRows[0].w, self.ModelRows[1].w, self.ModelRows[2].w, 1.0)
        );

    gl_Position = uniforms.ShadowMatrix * model * vec4(VertexPosition, 1.0);
}
