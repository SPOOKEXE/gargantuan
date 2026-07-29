#version 450

layout(location = 0) in vec2 FragmentUV;
layout(location = 0) out vec4 OutputColor;

// These bindings request temporal targets; reading Jitter enables projection jitter.
layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
// Previous accumulated output, not the raw render.
layout(set = 2, binding = 1) uniform sampler2D HistoryTexture;
// Texture-space previous-to-current motion.
layout(set = 2, binding = 2) uniform sampler2D VelocityTexture;
// Linear camera-forward distance in studs.
layout(set = 2, binding = 3) uniform sampler2D DepthTexture;
layout(set = 2, binding = 4) uniform sampler2D DepthHistoryTexture;

layout(set = 3, binding = 0) uniform Builtin {
    vec4 Resolution; // xy: pixels; zw: reciprocal pixels.
    vec4 Time;
    vec4 Jitter; // xy: current pixel offset; zw: previous pixel offset.
} builtin;

layout(set = 3, binding = 1) uniform Params {
    // x: history weight; zero selects the default.
    vec4 Feedback;
    // x: history clamp in standard deviations; zero selects the default.
    vec4 Clamping;
    // x: relative depth rejection threshold; zero selects the default.
    vec4 Disocclusion;
} params;

const float DEFAULT_FEEDBACK = 0.92;
const float DEFAULT_CLAMPING = 1.25;
const float DEFAULT_DISOCCLUSION = 0.1;

// YCoCg isolates luminance for tighter history clamping.
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

// Five-tap Catmull-Rom avoids repeated bilinear softening.
vec4 SampleHistory(vec2 uv) {
    vec2 resolution = builtin.Resolution.xy;
    vec2 texel = builtin.Resolution.zw;

    vec2 position = uv * resolution;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Combine middle weights into bilinear taps: sixteen reads become five.
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

    // Renormalize after dropping corner taps.
    result /= max(total, 0.0001);
    // Clamp negative Catmull-Rom ringing.
    return max(result, vec4(0.0));
}

// Nearest depth supplies silhouette motion instead of background motion.
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

// Match the current 3x3 nearest-depth rule to avoid false edge disocclusion.
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

    // Velocity is previous-to-current, so subtract for history reprojection.
    Nearest nearest = FindNearest(FragmentUV, texel);
    vec2 historyUv = FragmentUV - nearest.Velocity;

    // Reject history outside the prior viewport.
    if (historyUv.x < 0.0 || historyUv.x > 1.0 || historyUv.y < 0.0 || historyUv.y > 1.0) {
        OutputColor = current;
        return;
    }

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

    // Variance clipping resists a single bright neighbour widening the range.
    vec3 mean = sum / 9.0;
    vec3 deviation = sqrt(max(sumOfSquares / 9.0 - mean * mean, vec3(0.0)));
    vec3 minimum = max(mean - clamping * deviation, lowest);
    vec3 maximum = min(mean + clamping * deviation, highest);

    vec4 historySample = SampleHistory(historyUv);
    vec3 history = RgbToYCoCg(historySample.rgb);

    // Clip toward the centre along one line to preserve hue.
    vec3 offset = history - centre;
    vec3 extent = max(max(maximum - centre, centre - minimum), vec3(0.0001));
    vec3 units = abs(offset) / extent;
    float furthest = max(units.x, max(units.y, units.z));
    if (furthest > 1.0) {
        history = centre + offset / furthest;
    }

    // Centre-biased jitter samples carry more weight.
    float distanceFromCentre = length(builtin.Jitter.xy);
    float sampleWeight = exp(-2.29 * distanceFromCentre * distanceFromCentre);

    // Reject only nearer history; relative depth tolerates camera approach.
    float depthThen = NearestHistoryDepth(historyUv, texel);
    float revealed = max(nearest.Depth - depthThen, 0.0) / max(nearest.Depth, 0.0001);
    // Smooth rejection avoids threshold outlines.
    float believable = 1.0 - smoothstep(disocclusion, disocclusion * 3.0, revealed);

    // Luma weighting prevents bright samples dominating accumulation.
    float currentWeight = (1.0 - feedback) * sampleWeight / (1.0 + max(centre.x, 0.0));
    float historyWeight = believable * feedback / (1.0 + max(history.x, 0.0));

    vec3 resolved = (centre * currentWeight + history * historyWeight) / max(currentWeight + historyWeight, 0.0001);
    OutputColor = vec4(YCoCgToRgb(resolved), current.a);
}
