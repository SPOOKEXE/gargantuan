#version 450

// Swapchain-only debug overlay; excluded from camera outputs and caches.
// Discards the fullscreen triangle outside the target rectangle.

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D OverlayTexture;

layout(set = 3, binding = 0) uniform Overlay {
    // xy is the size of the whole target in pixels
    vec4 Target;
    // xy is the top-left corner of the picture in pixels, zw is its size
    vec4 Rect;
} overlay;

void main() {
    vec2 pixel = FragmentUV * overlay.Target.xy;
    vec2 local = (pixel - overlay.Rect.xy) / max(overlay.Rect.zw, vec2(1.0));

    if (local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) {
        discard;
    }

    // Alpha comes out as it went in; the pipeline blends it over what is
    // already on the swapchain
    OutputColor = texture(OverlayTexture, local);
}
