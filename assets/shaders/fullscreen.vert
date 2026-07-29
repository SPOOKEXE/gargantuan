#version 450

// Oversized fullscreen triangle; avoids a quad seam and needs no vertex buffer.

layout(location = 0) out vec2 FragmentUV;

void main() {
    // 0 -> (0,0), 1 -> (2,0), 2 -> (0,2)
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);

	// Flipped viewport keeps samples on their output row; UV * Resolution is pixel position.
    FragmentUV = vec2(corner.x, 1.0 - corner.y);
}
