#version 450

// The opaque fragment stage for the instanced pipeline.
//
// A copy of opaque.frag, differing only in where the two per-part surface
// values come from: varyings out of the instance buffer rather than a uniform
// block pushed for every part. Kept as a copy rather than shared through an
// include, because opaque.frag is what every non-instanced draw in the engine
// goes through and this change had no business touching it. If the lighting
// below ever changes, it has to change in both.


layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;
layout(location = 4) in vec2 FragmentUV;
layout(location = 5) flat in vec4 SurfaceNormal;
layout(location = 6) flat in vec4 SurfaceTransform;

layout(location = 0) out vec4 OutputColor;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowBiasMatrix;
    vec4 LightDirection;
} world;

layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;
// A part can show another camera's picture on its surface. Parts without one
// get a single white pixel, so the multiply below leaves them alone.
layout(set = 2, binding = 1) uniform sampler2D SurfaceTexture;

// Only the flag now, and it is pushed once per batch rather than once per part:
// every part in a batch shares a texture, so they share the answer to whether
// there is one. The two values that genuinely differ per part arrive above, out
// of the instance buffer.
layout(set = 3, binding = 1) uniform PartFragmentUniforms {
    vec4 HasSurfaceTexture;
    vec4 Unused0;
    vec4 Unused1;
} partFragment;

// How closely a fragment's normal has to match a flat face. Tight enough that a
// cube shows the picture on one face and a wedge does not bleed it onto the
// slope.
float SURFACE_FACE_MATCH = 0.9;
// How far off square a fragment can lean and still count as part of a curved
// side rather than one of the flat ends it runs between
float SURFACE_AROUND_MATCH = 0.5;

bool OnSurfaceFace(vec3 n) {
    float rule = SurfaceNormal.w;
    if (rule > 1.5) {
        return abs(dot(n, SurfaceNormal.xyz)) < SURFACE_AROUND_MATCH;
    }
    if (rule > 0.5) {
        return true;
    }
    return dot(n, SurfaceNormal.xyz) > SURFACE_FACE_MATCH;
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
        vec2 surfaceUV = (FragmentUV * SurfaceTransform.xy) + SurfaceTransform.zw;
        // Shown as-is rather than tinted, so a camera feed reads true
        surface = texture(SurfaceTexture, surfaceUV).rgb;
    }

    OutputColor = vec4(surface * lighting, FragmentColor.a);
}
