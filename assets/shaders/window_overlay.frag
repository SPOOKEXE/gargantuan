#version 450

// Lays a small picture over the finished window in pixel coordinates, for the
// engine's own debug readouts. Not a post-process pass: it runs after the
// camera's picture has been put on the swapchain, so what it draws is not in
// any camera's target, not in what Camera:Render() hands back, and not part of
// anything the redraw cache reasons about.
//
// The fullscreen triangle covers the whole target and everything outside the
// rectangle is discarded, rather than drawing a quad sized to the rectangle.
// The vertex stage is shared with every other pass and takes no vertex buffer,
// so there is nothing to size; a discard on the way out is cheaper than a
// pipeline that needs geometry of its own.

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
