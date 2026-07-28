#version 450

// Preset: a cheap fixed security camera. Wide lens, desaturated and cool,
// interlaced, noisy, and banded the way a low-bitrate encoder leaves things.
// Turn NightVision up and it becomes an infrared feed instead.
//
//   shader.Source = "security_camera"
//   shader:SetNumber("Distortion", 0.35)
//   shader:SetNumber("Scanlines", 1)
//   shader:SetNumber("Noise", 0.06)
//   shader:SetNumber("NightVision", 0)
//   shader:SetNumber("Contrast", 1.15)

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
    // Automatic gain, which is why a real security picture is bright and
    // noisy rather than simply dark
    vec4 Gain;
} params;

float Hash(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 centred = FragmentUV - 0.5;
    float squared = dot(centred, centred);

    // Wide lens: samples pushed outwards with the square of the radius
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
        // An IR sensor has no colour at all, and gain pushed up hard enough
        // that the picture is bright but grainy
        float gained = clamp(pow(luma, 0.8) * 1.15, 0.0, 1.0);

        // Cheap four-tap bloom, so bright things smear the way they do on a
        // sensor running wide open
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
        // Daylight feed: most of the colour gone, and what is left leans cool
        colour = mix(colour, vec3(luma), 0.72);
        colour *= vec3(0.92, 0.98, 1.06);

        // Gain applied with a curve, so shadows lift much more than highlights
        // and the picture does not simply clip
        float gain = max(params.Gain.x, 0.0001);
        colour = pow(clamp(colour, 0.0, 1.0), vec3(1.0 / gain));
        luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    }

    // Interlacing: alternate fields are a frame apart, so one set of rows is
    // dimmer than the other and the split crawls
    float field = mod(floor(row) + floor(time * 50.0), 2.0);
    float interlace = mix(1.0, 0.86, field * clamp(params.Scanlines.x, 0.0, 1.0));
    colour *= interlace;

    // Coarse luminance banding, standing in for a starved bitrate
    float levels = 24.0;
    colour = floor(colour * levels + 0.5) / levels;

    // Sensor noise, worse in the dark and worse still at night
    float noise = Hash(uv * builtin.Resolution.xy + time * 73.0) - 0.5;
    colour += noise * params.Noise.x * (1.0 + (1.0 - luma) * 1.5) * (1.0 + night);

    // Contrast about the midpoint, then the corners fall away
    colour = clamp((colour - 0.5) * max(params.Contrast.x, 0.0001) + 0.5, 0.0, 1.0);
    float vignette = clamp(1.0 - squared * 1.25, 0.0, 1.0);

    OutputColor = vec4(clamp(colour * vignette, 0.0, 1.0), 1.0);
}
