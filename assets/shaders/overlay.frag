#version 450

// Preset: pastes an EditableImage over the camera's output, respecting the
// overlay's own alpha.
//
//   local logo = Instance.new("EditableImage")
//   logo:Resize(Vector2.new(16, 16))
//   logo:DrawRectangle(Vector2.zero, Vector2.new(16, 16), Color3.new(0, 0, 1), 0)
//
//   local overlay = Instance.new("PostProcessShader")
//   overlay.Source = "overlay"
//   overlay:SetImage(logo)
//   overlay:SetNumber("Opacity", 1)                   -- slot 0
//   overlay:SetVector2("Position", Vector2.new(8, 8)) -- slot 1, top-left in pixels
//   camera:AddShader(overlay)

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 2, binding = 1) uniform sampler2D OverlayTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution;
    vec4 Time;
} builtin;

layout(set = 3, binding = 1) uniform Params {
    vec4 Opacity;
    vec4 Position;
} params;

void main() {
    vec4 source = texture(SourceTexture, FragmentUV);

    vec2 overlaySize = vec2(textureSize(OverlayTexture, 0));
    vec2 pixel = FragmentUV * builtin.Resolution.xy;
    vec2 local = pixel - params.Position.xy;

    // Outside the pasted rectangle nothing changes
    if (local.x < 0.0 || local.y < 0.0 || local.x >= overlaySize.x || local.y >= overlaySize.y) {
        OutputColor = source;
        return;
    }

    vec4 over = texture(OverlayTexture, local / overlaySize);
    float alpha = over.a * clamp(params.Opacity.x, 0.0, 1.0);
    OutputColor = vec4(mix(source.rgb, over.rgb, alpha), source.a);
}
