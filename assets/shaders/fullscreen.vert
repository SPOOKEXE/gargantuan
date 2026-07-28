#version 450

// Draws one oversized triangle covering the whole target. Cheaper than two
// triangles and avoids the seam along a quad's diagonal, so every post-process
// shader shares this vertex stage and only supplies a fragment shader.

layout(location = 0) out vec2 FragmentUV;

void main() {
    // 0 -> (0,0), 1 -> (2,0), 2 -> (0,2)
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);

    // SDL normalises to a top-left origin and flips the Vulkan viewport to get
    // there, so clip space y runs opposite to texture v. Flipping v here means
    // a fragment samples the same row it writes to, which keeps a passthrough
    // shader a true passthrough and makes FragmentUV * Resolution the
    // fragment's own pixel coordinate.
    FragmentUV = vec2(corner.x, 1.0 - corner.y);
}
