#version 450

layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;
layout(location = 4) in vec2 FragmentUV;

layout(location = 0) out vec4 OutputColor;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowBiasMatrix;
    vec4 LightDirection;
} world;

layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;
// Untextured parts receive a white fallback texture.
layout(set = 2, binding = 1) uniform sampler2D SurfaceTexture;

layout(set = 3, binding = 1) uniform PartFragmentUniforms {
    // x: surface texture present.
    vec4 HasSurfaceTexture;
    // xyz: world-space face; w: 0 facing, 1 all, 2 perpendicular.
    vec4 SurfaceNormal;
    // xy: tiling; zw: offset.
    vec4 SurfaceTransform;
} partFragment;

// Prevent flat-face textures bleeding onto wedge slopes.
float SURFACE_FACE_MATCH = 0.9;
// Separates curved sides from their flat ends.
float SURFACE_AROUND_MATCH = 0.5;

bool OnSurfaceFace(vec3 n) {
    float rule = partFragment.SurfaceNormal.w;
    if (rule > 1.5) {
        return abs(dot(n, partFragment.SurfaceNormal.xyz)) < SURFACE_AROUND_MATCH;
    }
    if (rule > 0.5) {
        return true;
    }
    return dot(n, partFragment.SurfaceNormal.xyz) > SURFACE_FACE_MATCH;
}

float SHADOW_SPREAD = 2.0;
vec2 SHADOW_TEXEL_SIZE = vec2(1.0 / 2048.0);
vec2 POISSON_DISK[4] = vec2[](
        vec2(-0.94201624, -0.39906216),
        vec2(0.94558609, -0.76890725),
        vec2(-0.094184101, -0.92938870),
        vec2(0.34495938, 0.29387760)
    );

void main() {
    vec3 shadowCoordinate = ShadowPosition.xyz / ShadowPosition.w;

    vec3 n = normalize(FragmentNormal);
    vec3 l = normalize(world.LightDirection.xyz);
    float nDotL = max(dot(n, l), 0.0);

    float bias = max(0.003 * (1.0 - nDotL), 0.0005);
    float currentDepth = shadowCoordinate.z - bias;

    float shadowFactor = 1.0;
    if (shadowCoordinate.x >= 0.0 && shadowCoordinate.x <= 1.0 &&
            shadowCoordinate.y >= 0.0 && shadowCoordinate.y <= 1.0 &&
            shadowCoordinate.z >= 0.0 && shadowCoordinate.z <= 1.0) {
        shadowFactor = 0.0;
        for (int i = 0; i < 4; i++) {
            vec2 sampleUV = shadowCoordinate.xy + (POISSON_DISK[i] * SHADOW_TEXEL_SIZE * SHADOW_SPREAD);
            shadowFactor += texture(ShadowMap, vec3(sampleUV, currentDepth));
        }
        shadowFactor /= 4;
    }

    float ambient = 0.2;
    float lighting = ambient + (nDotL * shadowFactor);

    vec3 surface = FragmentColor.rgb;
    if (partFragment.HasSurfaceTexture.x > 0.5 && OnSurfaceFace(n)) {
        vec2 surfaceUV = (FragmentUV * partFragment.SurfaceTransform.xy) + partFragment.SurfaceTransform.zw;
        // Surface feeds bypass part tint.
        surface = texture(SurfaceTexture, surfaceUV).rgb;
    }

    OutputColor = vec4(surface * lighting, FragmentColor.a);
}
