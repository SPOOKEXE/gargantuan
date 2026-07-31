#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution;
    vec4 Time;
} builtin;

layout(set = 3, binding = 1) uniform Params {
    vec4 Distortion;
    vec4 Scanlines;
    vec4 Noise;
    vec4 NightVision;
    vec4 Contrast;
    vec4 Gain;
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

    vec3 colour = texture(SourceTexture, uv).rgb;
    float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    float time = builtin.Time.x;
    float row = uv.y * builtin.Resolution.y;

    float night = clamp(params.NightVision.x, 0.0, 1.0);
    if (night > 0.0) {
        float gained = clamp(pow(luma, 0.8) * 1.15, 0.0, 1.0);

        vec2 spread = 2.5 / builtin.Resolution.xy;
        float glow = 0.0;
        glow += dot(texture(SourceTexture, uv + vec2(spread.x, 0.0)).rgb, vec3(0.333));
        glow += dot(texture(SourceTexture, uv - vec2(spread.x, 0.0)).rgb, vec3(0.333));
        glow += dot(texture(SourceTexture, uv + vec2(0.0, spread.y)).rgb, vec3(0.333));
        glow += dot(texture(SourceTexture, uv - vec2(0.0, spread.y)).rgb, vec3(0.333));
        gained += max(glow * 0.25 - 0.45, 0.0) * 0.8;

        vec3 infrared = vec3(0.15, 1.0, 0.35) * gained;
        colour = mix(colour, infrared, night);
        luma = gained;
    } else {
        colour = mix(colour, vec3(luma), 0.72);
        colour *= vec3(0.92, 0.98, 1.06);

        float gain = max(params.Gain.x, 0.0001);
        colour = pow(clamp(colour, 0.0, 1.0), vec3(1.0 / gain));
        luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    }

    float field = mod(floor(row) + floor(time * 50.0), 2.0);
    float interlace = mix(1.0, 0.86, field * clamp(params.Scanlines.x, 0.0, 1.0));
    colour *= interlace;

    float levels = 24.0;
    colour = floor(colour * levels + 0.5) / levels;

    float noise = Hash(uv * builtin.Resolution.xy + time * 73.0) - 0.5;
    colour += noise * params.Noise.x * (1.0 + (1.0 - luma) * 1.5) * (1.0 + night);

    colour = clamp((colour - 0.5) * max(params.Contrast.x, 0.0001) + 0.5, 0.0, 1.0);
    float vignette = clamp(1.0 - squared * 1.25, 0.0, 1.0);

    OutputColor = vec4(clamp(colour * vignette, 0.0, 1.0), 1.0);
}
