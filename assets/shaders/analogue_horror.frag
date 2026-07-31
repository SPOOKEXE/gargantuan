#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 3, binding = 0) uniform Builtin { vec4 Resolution; vec4 Time; } builtin;
layout(set = 3, binding = 1) uniform Params {
    vec4 Intensity;
    vec4 Noise;
    vec4 Tracking;
} params;

float Hash(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float time = builtin.Time.x;
    float intensity = clamp(params.Intensity.x, 0.0, 1.0);
    vec2 uv = FragmentUV;

    float row = uv.y * builtin.Resolution.y;
    float wobble = sin(uv.y * 90.0 + time * 6.0) * 0.0016
        + sin(uv.y * 13.0 - time * 2.0) * 0.0035;

    float band = step(0.985, Hash(vec2(floor(row / 6.0), floor(time * 8.0))));
    wobble += band * 0.02 * params.Tracking.x;
    uv.x += wobble * params.Tracking.x * intensity;

    float bleed = (0.0022 + band * 0.004) * intensity;
    vec3 colour = vec3(
            texture(SourceTexture, uv + vec2(bleed, 0.0)).r,
            texture(SourceTexture, uv).g,
            texture(SourceTexture, uv - vec2(bleed, 0.0)).b
        );

    float scanline = 0.82 + 0.18 * sin(row * 3.14159);
    float roll = 0.94 + 0.06 * sin((uv.y + time * 0.12) * 6.28318);
    colour *= mix(1.0, scanline * roll, intensity);

    float grain = Hash(uv * builtin.Resolution.xy + time * 60.0);
    colour += (grain - 0.5) * params.Noise.x * intensity;

    float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = mix(colour, vec3(luma), 0.35 * intensity);
    colour *= mix(vec3(1.0), vec3(0.86, 1.02, 0.90), intensity);
    colour = mix(colour, clamp((colour - 0.06) * 1.18, 0.0, 1.0), intensity);

    vec2 centred = FragmentUV - 0.5;
    float vignette = 1.0 - dot(centred, centred) * 1.5 * intensity;
    OutputColor = vec4(clamp(colour * vignette, 0.0, 1.0), 1.0);
}
