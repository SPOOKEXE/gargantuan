#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 3, binding = 0) uniform Builtin { vec4 Resolution; vec4 Time; } builtin;
layout(set = 3, binding = 1) uniform Params {
    vec4 Distortion;
    vec4 Vignette;
    vec4 Grain;
} params;

float Hash(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 centred = FragmentUV - 0.5;
    float squared = dot(centred, centred);

    vec2 uv = 0.5 + centred * (1.0 + params.Distortion.x * squared);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        OutputColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Radial RGB offsets mimic lateral chromatic aberration.
    float fringe = 0.0015 * params.Distortion.x;
    vec3 colour = vec3(
            texture(SourceTexture, 0.5 + (uv - 0.5) * (1.0 + fringe)).r,
            texture(SourceTexture, uv).g,
            texture(SourceTexture, 0.5 + (uv - 0.5) * (1.0 - fringe)).b
        );

    float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = mix(colour, vec3(luma), 0.25);
    colour *= vec3(1.06, 1.0, 0.92);

    float grain = Hash(FragmentUV * builtin.Resolution.xy + builtin.Time.x * 91.0);
    colour += (grain - 0.5) * params.Grain.x;

    float vignette = clamp(1.0 - squared * 2.6 * params.Vignette.x, 0.0, 1.0);
    OutputColor = vec4(clamp(colour * vignette, 0.0, 1.0), 1.0);
}
