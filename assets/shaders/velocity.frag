#version 450

// Output 0: texture-space motion for `uv - velocity` history reprojection.
// Output 1: linear camera-forward distance in studs.

layout(location = 0) in vec4 CurrentClip;
layout(location = 1) in vec4 PreviousClip;

layout(location = 0) out vec2 OutVelocity;
layout(location = 1) out float OutDepth;

void main() {
    // Clip w is linear camera-forward distance.
    OutDepth = CurrentClip.w;

    // Prior points behind the eye emit zero motion; temporal passes reject them.
    if (PreviousClip.w <= 0.0) {
        OutVelocity = vec2(0.0);
        return;
    }

    vec2 current = CurrentClip.xy / CurrentClip.w;
    vec2 previous = PreviousClip.xy / PreviousClip.w;
    vec2 step = current - previous;

	// NDC spans two units; texture space spans one and has top-left origin.
    OutVelocity = vec2(0.5 * step.x, -0.5 * step.y);
}
