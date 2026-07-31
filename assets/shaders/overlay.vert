#version 450

// Rect is the panel in 0..1 screen space with the origin top left: xy is the
// corner, zw the size. Kept in pixels-over-viewport rather than NDC so the
// panel is laid out in the same units it was drawn in.
layout(set = 1, binding = 0) uniform OverlayUniforms {
    vec4 Rect;
} overlay;

layout(location = 0) out vec2 FragmentUV;

// No vertex buffer is bound: six indices are cheaper to generate here than a
// buffer to hold them, and the pass owns exactly one quad.
const vec2 CORNERS[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 corner = CORNERS[gl_VertexIndex];
    FragmentUV = corner;

    // Rect is laid out with y growing downwards, the direction the panel was
    // drawn in; clip space here has y growing up. Negating y rather than
    // flipping the UV keeps the texture upright: the quad's top edge and the
    // image's first row end up at the same place, which is the whole trick.
    vec2 position = overlay.Rect.xy + corner * overlay.Rect.zw;
    gl_Position = vec4(position.x * 2.0 - 1.0, 1.0 - position.y * 2.0, 0.0, 1.0);
}
