#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

// OverlayTexture is image slot 1. Params pack in Set order: Opacity, Position.
layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 2, binding = 1) uniform sampler2D OverlayTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution;
    vec4 Time;
} builtin;

layout(set = 3, binding = 1) uniform Params {
    vec4 Opacity;
    // Top-left pixel coordinate.
    vec4 Position;
} params;

void main() {
    vec4 source = texture(SourceTexture, FragmentUV);

    vec2 overlaySize = vec2(textureSize(OverlayTexture, 0));
    vec2 pixel = FragmentUV * builtin.Resolution.xy;
    vec2 local = pixel - params.Position.xy;

    if (local.x < 0.0 || local.y < 0.0 || local.x >= overlaySize.x || local.y >= overlaySize.y) {
        OutputColor = source;
        return;
    }

    vec4 over = texture(OverlayTexture, local / overlaySize);
    float alpha = over.a * clamp(params.Opacity.x, 0.0, 1.0);
    OutputColor = vec4(mix(source.rgb, over.rgb, alpha), source.a);
}
