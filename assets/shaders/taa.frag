#version 450

// Temporal antialiasing. Meant to be swapped in for the built-in edge blur:
//
//   local taa = Instance.new("PostProcessShader")
//   taa.Preset = Enum.PresetShaders.TemporalAntialias
//   taa:SetRenderTexture(Enum.ShaderProperty.HistoryTexture, Enum.RenderTexture.History)
//   taa:SetRenderTexture(Enum.ShaderProperty.VelocityTexture, Enum.RenderTexture.Velocity)
//   taa:SetRenderTexture(Enum.ShaderProperty.DepthTexture, Enum.RenderTexture.Depth)
//   taa:SetRenderTexture(Enum.ShaderProperty.DepthHistoryTexture, Enum.RenderTexture.DepthHistory)
//   RenderSettings.AntialiasShader = taa
//
// The bindings are what make it work at all, and asking for them is what makes
// the engine produce them: History is the camera's own finished picture from
// last frame, Velocity is where each pixel was on that frame, and the two
// depths are how far away things are now and were then. Reading builtin.Jitter
// is separately what puts the camera's projection on a sub-pixel wander, so
// each frame samples a different point inside the pixel.
//
// Where FXAA guesses at an edge from one frame and blurs along it, this one
// averages the frames themselves. Every frame samples the pixel somewhere
// slightly different, so a handful of them together describe how much of the
// pixel the edge actually covers -- which is what antialiasing is, rather than
// a smear that happens to look like it.
//
// Everything hard about this is history that should not be reused: a pixel the
// camera swung past, a surface that moved out from behind another, a light that
// changed. Velocity finds where a point was; two tests then decide whether what
// was there is still worth believing.
//
// The colour test asks whether the old sample could plausibly belong among this
// pixel's neighbours. It catches most things and is cheap, but it is blind in
// exactly one case: a surface emerging from behind another of much the same
// shade. The motion is honest, the history is wrong, and the two colours agree.
// The depth test is what sees that -- whatever stood at that spot last frame
// was nearer than what stands here now, and no amount of looking at colour
// says so.

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
// The camera's own last frame, after this pass ran on it -- the accumulation,
// not the raw render
layout(set = 2, binding = 1) uniform sampler2D HistoryTexture;
// Texture-space step from the previous frame's position to this one's
layout(set = 2, binding = 2) uniform sampler2D VelocityTexture;
// Distance from the camera in studs, this frame and last
layout(set = 2, binding = 3) uniform sampler2D DepthTexture;
layout(set = 2, binding = 4) uniform sampler2D DepthHistoryTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution; // xy = pixels, zw = 1/pixels
    vec4 Time;
    vec4 Jitter; // xy = this frame's sub-pixel offset in pixels, zw = last frame's
} builtin;

layout(set = 3, binding = 1) uniform Params {
    // x: how much of the picture is carried over from last frame. Higher
    // resolves smoother and settles slower, and drags harder on anything the
    // neighbourhood test fails to catch. Zero asks for the default.
    vec4 Feedback;
    // x: how far outside the local range of colours the history is allowed to
    // sit before it is pulled back in, in standard deviations. Lower is
    // stricter: less ghosting, more of the accumulation thrown away. Zero asks
    // for the default.
    vec4 Clamping;
    // x: how far the two depths may disagree, as a fraction of how far away the
    // surface is, before the history is treated as belonging to something else.
    // Relative rather than absolute because a camera closing on a wall changes
    // its distance by a little and a wall giving way to the room behind it
    // changes by a lot, whatever the scale of the place. Zero asks for the
    // default.
    vec4 Disocclusion;
} params;

const float DEFAULT_FEEDBACK = 0.92;
const float DEFAULT_CLAMPING = 1.25;
const float DEFAULT_DISOCCLUSION = 0.1;

// Luma and two chroma axes, from a rotation of RGB that costs a few adds.
// Clamping in RGB tests three channels that move together, so a colour can sit
// inside all three ranges and still be wrong; in YCoCg brightness is one axis
// on its own, which is the axis ghosting actually travels along.
vec3 RgbToYCoCg(vec3 colour) {
    return vec3(
        0.25 * colour.r + 0.5 * colour.g + 0.25 * colour.b,
        0.5 * colour.r - 0.5 * colour.b,
        -0.25 * colour.r + 0.5 * colour.g - 0.25 * colour.b
    );
}

vec3 YCoCgToRgb(vec3 colour) {
    float t = colour.x - colour.z;
    return vec3(t + colour.y, colour.x + colour.z, t - colour.y);
}

// Bicubic rather than bilinear, because the history is resampled every single
// frame. Bilinear loses a little every time and the loss compounds: within a
// second of the camera drifting, a linear-sampled history is visibly soft.
vec4 SampleHistory(vec2 uv) {
    vec2 resolution = builtin.Resolution.xy;
    vec2 texel = builtin.Resolution.zw;

    vec2 position = uv * resolution;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre;

    // Catmull-Rom weights for the four taps along each axis
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // The middle two taps of each axis are fetched as one bilinear sample
    // placed between them, which is what turns sixteen reads into five
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / max(w12, vec2(0.0001));

    vec2 uv0 = (centre - 1.0) * texel;
    vec2 uv3 = (centre + 2.0) * texel;
    vec2 uv12 = (centre + offset12) * texel;

    vec4 result = vec4(0.0);
    float total = 0.0;

    float weight = w12.x * w0.y;
    result += texture(HistoryTexture, vec2(uv12.x, uv0.y)) * weight;
    total += weight;

    weight = w0.x * w12.y;
    result += texture(HistoryTexture, vec2(uv0.x, uv12.y)) * weight;
    total += weight;

    weight = w12.x * w12.y;
    result += texture(HistoryTexture, vec2(uv12.x, uv12.y)) * weight;
    total += weight;

    weight = w3.x * w12.y;
    result += texture(HistoryTexture, vec2(uv3.x, uv12.y)) * weight;
    total += weight;

    weight = w12.x * w3.y;
    result += texture(HistoryTexture, vec2(uv12.x, uv3.y)) * weight;
    total += weight;

    // The corner taps are dropped, so the weights no longer sum to one
    result /= max(total, 0.0001);
    // Catmull-Rom overshoots at a hard edge and can ring below black
    return max(result, vec4(0.0));
}

// The nearest surface in the pixel's own neighbourhood, and how it moved.
//
// Taking the centre pixel's own motion looks obviously right and is not. An
// edge is antialiased across two pixels, so the pixel holding the near side of
// a silhouette often sits over the background by more than half its area and
// reports the background's motion; the near surface then reprojects to where
// the far one was and trails behind itself. Whatever is closest is what the eye
// reads the pixel as, so its motion is the one to follow.
//
// The distance comes back with it because the two have to agree. A test asking
// whether the history belongs here has to be asking about the same surface the
// reprojection went looking for.
struct Nearest {
    vec2 Velocity;
    float Depth;
};

Nearest FindNearest(vec2 uv, vec2 texel) {
    vec2 closest = uv;
    float nearest = texture(DepthTexture, uv).r;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 at = uv + vec2(x, y) * texel;
            float depth = texture(DepthTexture, at).r;
            if (depth < nearest) {
                nearest = depth;
                closest = at;
            }
        }
    }

    return Nearest(texture(VelocityTexture, closest).xy, nearest);
}

// The same question asked of last frame's distances, and it has to be asked the
// same way. A single tap would hold the nearest thing anywhere around here
// against whatever happened to land on one particular texel back then, and at
// an edge those are not comparable: the camera samples a fraction of a pixel
// further along every frame, so a texel just outside a silhouette holds the
// surface on one frame and the emptiness behind it on the next. Every edge in
// the picture would read as a disocclusion and throw away its own accumulation.
float NearestHistoryDepth(vec2 uv, vec2 texel) {
    float nearest = texture(DepthHistoryTexture, uv).r;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            nearest = min(nearest, texture(DepthHistoryTexture, uv + vec2(x, y) * texel).r);
        }
    }

    return nearest;
}

void main() {
    vec2 texel = builtin.Resolution.zw;
    vec4 current = texture(SourceTexture, FragmentUV);

    float feedback = params.Feedback.x <= 0.0 ? DEFAULT_FEEDBACK : clamp(params.Feedback.x, 0.0, 0.98);
    float clamping = params.Clamping.x <= 0.0 ? DEFAULT_CLAMPING : params.Clamping.x;
    float disocclusion = params.Disocclusion.x <= 0.0 ? DEFAULT_DISOCCLUSION : params.Disocclusion.x;

    // Where this point was last frame. Zero on the background, which nothing
    // was drawn over, so it reprojects onto itself.
    Nearest nearest = FindNearest(FragmentUV, texel);
    vec2 historyUv = FragmentUV - nearest.Velocity;

    // It was off the side of the screen, so there is no history of it to
    // accumulate. Nothing to do but show this frame.
    if (historyUv.x < 0.0 || historyUv.x > 1.0 || historyUv.y < 0.0 || historyUv.y > 1.0) {
        OutputColor = current;
        return;
    }

    // The range of colours this pixel's own surroundings take, which is the
    // test for whether the history still belongs here. A sample that came from
    // a surface since covered up, or lit differently, or moved off, lands
    // outside what any of its neighbours could be.
    vec3 centre = RgbToYCoCg(current.rgb);
    vec3 sum = vec3(0.0);
    vec3 sumOfSquares = vec3(0.0);
    vec3 lowest = vec3(1e6);
    vec3 highest = vec3(-1e6);

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 neighbour = RgbToYCoCg(texture(SourceTexture, FragmentUV + vec2(x, y) * texel).rgb);
            sum += neighbour;
            sumOfSquares += neighbour * neighbour;
            lowest = min(lowest, neighbour);
            highest = max(highest, neighbour);
        }
    }

    // Mean and spread rather than the outright minimum and maximum. One bright
    // pixel in the nine drags the hard range wide enough to let a stale sample
    // through; the spread barely notices it.
    vec3 mean = sum / 9.0;
    vec3 deviation = sqrt(max(sumOfSquares / 9.0 - mean * mean, vec3(0.0)));
    vec3 minimum = max(mean - clamping * deviation, lowest);
    vec3 maximum = min(mean + clamping * deviation, highest);

    vec4 historySample = SampleHistory(historyUv);
    vec3 history = RgbToYCoCg(historySample.rgb);

    // Pulled back towards the middle of the range along the line it sits on,
    // rather than squashed onto the box per channel. Clamping each axis on its
    // own moves the colour sideways as well as in, which shows up as the wrong
    // hue along a moving edge.
    vec3 offset = history - centre;
    vec3 extent = max(max(maximum - centre, centre - minimum), vec3(0.0001));
    vec3 units = abs(offset) / extent;
    float furthest = max(units.x, max(units.y, units.z));
    if (furthest > 1.0) {
        history = centre + offset / furthest;
    }

    // How much this frame is worth saying about this pixel. It was sampled
    // wherever the jitter put it, and a sample taken near the middle of the
    // pixel describes that pixel better than one taken out by its corner, so
    // the offset the camera used is worth something here rather than being
    // merely the thing that made the accumulation possible.
    float distanceFromCentre = length(builtin.Jitter.xy);
    float sampleWeight = exp(-2.29 * distanceFromCentre * distanceFromCentre);

    // What stood at that spot last frame, against what stands here now. The
    // colour test above cannot separate a surface coming out from behind
    // another of the same shade from that same surface simply sitting there;
    // the distances can, because the thing that was covering it was nearer.
    //
    // Relative, so a camera closing on a surface is not mistaken for one: a
    // frame of approach changes the distance by a fraction of a percent, and
    // an occluder giving way to what is behind it changes it by a great deal.
    //
    // One-sided, and that is the whole of it. Only a history that was NEARER
    // than what stands here now is a disocclusion -- something was in the way
    // and has moved off. The other direction, a history further away than what
    // is here, is something arriving in front, and there the colour test needs
    // no help: a surface that has just covered this pixel looks nothing like
    // what the pixel used to be. Testing both directions would also make every
    // silhouette flicker, because the distance behind a shape is the far plane
    // and a jittering camera moves the edge across the pixel each frame.
    float depthThen = NearestHistoryDepth(historyUv, texel);
    float revealed = max(nearest.Depth - depthThen, 0.0) / max(nearest.Depth, 0.0001);
    // Eased rather than cut, because a hard threshold draws its own visible
    // outline around everything that fails it
    float believable = 1.0 - smoothstep(disocclusion, disocclusion * 3.0, revealed);

    // Weighted against brightness, so one frame of a bright highlight cannot
    // outvote the several dimmer frames around it. Without this a specular
    // glint crawling across a surface flickers rather than resolving.
    float currentWeight = (1.0 - feedback) * sampleWeight / (1.0 + max(centre.x, 0.0));
    float historyWeight = believable * feedback / (1.0 + max(history.x, 0.0));

    vec3 resolved = (centre * currentWeight + history * historyWeight) / max(currentWeight + historyWeight, 0.0001);
    OutputColor = vec4(YCoCgToRgb(resolved), current.a);
}
