#version 450

layout(location = 0) in vec2 FragmentUV;

layout(set = 2, binding = 0) uniform sampler2D OverlayTexture;

layout(location = 0) out vec4 OutputColor;

void main() {
    // Straight alpha, composited by the pipeline's blend state. The panel is
    // drawn on the CPU, so there is nothing to do here but hand it over.
    OutputColor = texture(OverlayTexture, FragmentUV);
}
