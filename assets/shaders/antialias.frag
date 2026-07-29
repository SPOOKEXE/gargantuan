#version 450

// FXAA-style final pass. Flat areas below threshold remain bit-for-bit unchanged.

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution; // xy = pixels, zw = 1/pixels
    vec4 Time;
} builtin;

layout(set = 3, binding = 1) uniform Params {
    // x: contrast below which a pixel is left exactly as it was
    vec4 Threshold;
} params;

float Luminance(vec3 colour) {
    return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 texel = builtin.Resolution.zw;
    vec4 centre = texture(SourceTexture, FragmentUV);

    float lumaCentre = Luminance(centre.rgb);
    float lumaNorth = Luminance(texture(SourceTexture, FragmentUV + vec2(0.0, -texel.y)).rgb);
    float lumaSouth = Luminance(texture(SourceTexture, FragmentUV + vec2(0.0, texel.y)).rgb);
    float lumaWest = Luminance(texture(SourceTexture, FragmentUV + vec2(-texel.x, 0.0)).rgb);
    float lumaEast = Luminance(texture(SourceTexture, FragmentUV + vec2(texel.x, 0.0)).rgb);

    float lowest = min(lumaCentre, min(min(lumaNorth, lumaSouth), min(lumaWest, lumaEast)));
    float highest = max(lumaCentre, max(max(lumaNorth, lumaSouth), max(lumaWest, lumaEast)));
    float contrast = highest - lowest;

    // Flat enough to be no edge at all, so hand back exactly what came in
    float threshold = max(params.Threshold.x, 0.0001);
    if (contrast < threshold) {
        OutputColor = centre;
        return;
    }

    // Step perpendicular to the larger luma gradient, along the edge.
    float horizontal = abs(lumaWest + lumaEast - 2.0 * lumaCentre);
    float vertical = abs(lumaNorth + lumaSouth - 2.0 * lumaCentre);
    vec2 direction = vertical >= horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

    vec4 blurred = 0.5 * centre
        + 0.25 * texture(SourceTexture, FragmentUV - direction)
        + 0.25 * texture(SourceTexture, FragmentUV + direction);

    // Ease it in with the contrast, so a faint edge is barely touched
    float strength = clamp((contrast - threshold) / max(1.0 - threshold, 0.0001), 0.0, 1.0);
    OutputColor = mix(centre, blurred, strength);
}
