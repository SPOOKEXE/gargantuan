#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution; // xy: pixels; zw: reciprocal pixels.
    vec4 Time;       // x: seconds since start.
} builtin;

// One vec4 per parameter, in first-set order reported by ListParameters().
layout(set = 3, binding = 1) uniform Params {
    vec4 Intensity;
    vec4 Tint;
} params;

void main() {
    vec4 source = texture(SourceTexture, FragmentUV);

    vec2 centered = FragmentUV - 0.5;
    float falloff = 1.0 - dot(centered, centered) * 2.0 * params.Intensity.x;
    falloff = clamp(falloff, 0.0, 1.0);

    OutputColor = vec4(source.rgb * falloff * params.Tint.rgb, source.a);
}
