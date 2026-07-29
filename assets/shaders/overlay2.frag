#version 450

// Image bindings follow first-set order reported by ListImages().

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
layout(set = 2, binding = 1) uniform sampler2D FirstTexture;
layout(set = 2, binding = 2) uniform sampler2D SecondTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution;
    vec4 Time;
} builtin;

layout(set = 3, binding = 1) uniform Params {
    vec4 FirstPosition;
    vec4 SecondPosition;
} params;

vec4 Paste(vec4 under, sampler2D image, vec2 pixel, vec2 origin) {
    vec2 size = vec2(textureSize(image, 0));
    vec2 local = pixel - origin;
    if (local.x < 0.0 || local.y < 0.0 || local.x >= size.x || local.y >= size.y) {
        return under;
    }

    vec4 over = texture(image, local / size);
    return vec4(mix(under.rgb, over.rgb, over.a), under.a);
}

void main() {
    vec2 pixel = FragmentUV * builtin.Resolution.xy;
    vec4 colour = texture(SourceTexture, FragmentUV);
    colour = Paste(colour, FirstTexture, pixel, params.FirstPosition.xy);
    colour = Paste(colour, SecondTexture, pixel, params.SecondPosition.xy);
    OutputColor = colour;
}
