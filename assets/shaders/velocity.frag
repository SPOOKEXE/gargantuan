#version 450

// The screen-space step from where this point was to where it is, in texture
// coordinates, so a pass sampling history at `uv - velocity` lands on the same
// surface point it is shading now.

layout(location = 0) in vec4 CurrentClip;
layout(location = 1) in vec4 PreviousClip;

layout(location = 0) out vec2 OutVelocity;

void main() {
    // Behind the eye last frame, so it had no position on screen to have come
    // from. Zero means "do not reproject"; a temporal pass rejects the history
    // it finds there on colour instead, which is the honest answer.
    if (PreviousClip.w <= 0.0) {
        OutVelocity = vec2(0.0);
        return;
    }

    vec2 current = CurrentClip.xy / CurrentClip.w;
    vec2 previous = PreviousClip.xy / PreviousClip.w;
    vec2 step = current - previous;

    // Clip space spans two units across the target and runs y-up; SDL flips the
    // viewport to a top-left origin, so v counts the other way. Halving turns
    // the span into the 0..1 of a texture coordinate.
    OutVelocity = vec2(0.5 * step.x, -0.5 * step.y);
}
