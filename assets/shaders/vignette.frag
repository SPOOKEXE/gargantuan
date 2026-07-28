#version 450

// An example PostProcessShader. Darkens towards the edges and tints the
// result, to show the shape a camera post-process shader takes.
//
//   local shader = Instance.new("PostProcessShader")
//   shader.Source = "vignette"
//   shader:SetNumber("Intensity", 0.8)   -- first parameter set, slot 0
//   shader:SetColor3("Tint", Color3.new(1, 0.9, 0.8))  -- slot 1
//   camera:AddShader(shader)

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution; // xy = pixels, zw = 1/pixels
    vec4 Time;       // x = seconds since start
} builtin;

// One vec4 slot per parameter, in the order they were first set from Luau.
// ShaderScript:ListParameters() reports that order.
layout(set = 3, binding = 1) uniform Params {
    vec4 Intensity;
    vec4 Tint;
} params;

void main() {
    vec4 source = texture(SourceTexture, FragmentUV);

    vec2 centered = FragmentUV - 0.5;
    float falloff = 1.0 - dot(centered, centered) * 2.0 * params.Intensity.x;
    falloff = clamp(falloff, 0.0, 1.0);

    OutputColor = vec4(source.rgb * falloff * params.Tint.rgb, source.a);
}
