#version 450

// Preset surface shader: wraps an image around every object the camera draws,
// projected from the object's world position. Shows how a surface shader takes
// images the way a post-process one does.
//
//   local skin = Instance.new("EditableImage")
//   skin:Resize(Vector2.new(32, 32))
//   -- ...draw into it...
//
//   local textured = Instance.new("SurfaceShader")
//   textured.Source = "surface_textured"
//   textured:SetImage("Skin", skin)     -- sampler slot 1, after the shadow map
//   textured:SetNumber("Scale", 0.25)
//   camera.SurfaceShader = textured

layout(location = 0) in vec3 FragmentNormal;
layout(location = 1) in vec4 FragmentColor;
layout(location = 2) in vec4 WorldPosition;
layout(location = 3) in vec4 ShadowPosition;

layout(location = 0) out vec4 OutputColor;

// Slot 0 is the shadow map the engine always binds
layout(set = 2, binding = 0) uniform sampler2DShadow ShadowMap;
layout(set = 2, binding = 1) uniform sampler2D Skin;

layout(set = 3, binding = 0) uniform WorldUniforms {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ShadowBiasMatrix;
    vec4 LightDirection;
} world;

layout(set = 3, binding = 1) uniform Params {
    vec4 Scale;
} params;

void main() {
    // Project along whichever axis the surface faces most, so the image lands
    // the right way up on every side of a block
    vec3 normal = abs(normalize(FragmentNormal));
    vec2 uv;
    if (normal.y > normal.x && normal.y > normal.z) {
        uv = WorldPosition.xz;
    } else if (normal.x > normal.z) {
        uv = WorldPosition.zy;
    } else {
        uv = WorldPosition.xy;
    }

    vec4 skin = texture(Skin, uv * max(params.Scale.x, 0.0001));
    float lighting = 0.2 + max(dot(normalize(FragmentNormal), normalize(world.LightDirection.xyz)), 0.0);
    OutputColor = vec4(FragmentColor.rgb * skin.rgb * lighting, FragmentColor.a);
}
