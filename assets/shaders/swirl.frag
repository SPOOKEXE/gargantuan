#version 450

// Preset: twists the picture around its centre, strongest in the middle and
// easing out to nothing at Radius.
//   shader:SetNumber("Strength", 3)
//   shader:SetNumber("Radius", 0.45)

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 3, binding = 0) uniform Builtin { vec4 Resolution; vec4 Time; } builtin;
layout(set = 3, binding = 1) uniform Params {
    vec4 Strength;
    vec4 Radius;
} params;

void main() {
    // Work in square units, or the twist comes out elliptical on a wide image
    float aspect = builtin.Resolution.x / max(builtin.Resolution.y, 1.0);
    vec2 offset = (FragmentUV - 0.5) * vec2(aspect, 1.0);

    float radius = max(params.Radius.x, 0.0001);
    float distance = length(offset);

    if (distance < radius) {
        // Smooth falloff, so there is no visible seam at the rim
        float falloff = 1.0 - distance / radius;
        float angle = params.Strength.x * falloff * falloff;
        float s = sin(angle);
        float c = cos(angle);
        offset = vec2(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
    }

    OutputColor = texture(SourceTexture, offset / vec2(aspect, 1.0) + 0.5);
}
