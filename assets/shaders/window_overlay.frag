#version 450

// Swapchain-only; excluded from camera outputs and caches.

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D OverlayTexture;

layout(set = 3, binding = 0) uniform Overlay {
    // xy: target size in pixels.
    vec4 TargetSizePixels;
    // xy: top-left in pixels; zw: size in pixels.
    vec4 RectPixels;
} overlay;

void main() {
    vec2 pixel = FragmentUV * overlay.TargetSizePixels.xy;
    vec2 local = (pixel - overlay.RectPixels.xy) / max(overlay.RectPixels.zw, vec2(1.0));

    if (local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) {
        discard;
    }

    OutputColor = texture(OverlayTexture, local);
}
