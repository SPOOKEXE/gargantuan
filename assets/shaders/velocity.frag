#version 450

// Two measurements of the same fragment, written together because the geometry
// only has to be drawn once to have both.
//
//   0  the screen-space step from where this point was to where it is, in
//      texture coordinates, so a pass sampling history at `uv - velocity`
//      lands on the same surface point it is shading now
//   1  how far the point is from the camera, in studs

layout(location = 0) in vec4 CurrentClip;
layout(location = 1) in vec4 PreviousClip;

layout(location = 0) out vec2 OutVelocity;
layout(location = 1) out float OutDepth;

void main() {
    // A perspective projection divides by the distance along the direction the
    // camera faces, so w already is that distance. Linear, unlike what the
    // depth buffer keeps, which means it can be compared between frames
    // without anyone having to know the clip planes to undo the curve.
    OutDepth = CurrentClip.w;

    // Behind the eye last frame, so it had no position on screen to have come
    // from. Zero means "do not reproject"; a temporal pass rejects the history
    // it finds there on colour and depth instead, which is the honest answer.
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
