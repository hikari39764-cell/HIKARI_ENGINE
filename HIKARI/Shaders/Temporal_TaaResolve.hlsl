cbuffer TemporalTaaCB : register(b0)
{
    float4 gScreenParams; // xy: render size, zw: inverse render size
    float4 gTaaParams;    // x: history valid, y: history weight, z: exposure valid, w: clip gamma
    float4 gRejectParams; // x: depth threshold, y: luma threshold, z: sharpness, w: debug mode
};

Texture2D<float4> gCurrentColor : register(t0);
Texture2D<float4> gHistoryColor : register(t1);
Texture2D<float2> gMotionVectors : register(t2);
Texture2D<float2> gMotionMetadata : register(t3);
Texture2D<float> gSceneDepth : register(t4);
Texture2D<float> gHistoryDepth : register(t5);
Texture2D<float> gReactiveMask : register(t6);
Texture2D<float> gTransparencyMask : register(t7);
Texture2D<float> gInvalidDepthMotionMask : register(t8);
Texture2D<float> gExposure : register(t9);
SamplerState gLinearClamp : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSOut
{
    float4 resolvedColor : SV_TARGET0;
    float4 historyColor : SV_TARGET1;
    float historyDepth : SV_TARGET2;
    float4 debugColor : SV_TARGET3;
};

struct MotionSample
{
    float2 pixels;
    float expectedPreviousDepth;
    float valid;
};

struct RejectionResult
{
    float depth;
    float luma;
    float confidence;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 RGBToYCoCg(float3 color)
{
    return float3(
        dot(color, float3(0.25f, 0.5f, 0.25f)),
        color.r - color.b,
        color.g - dot(color.rb, float2(0.5f, 0.5f)));
}

float3 YCoCgToRGB(float3 color)
{
    float chromaBase = color.x - color.z * 0.5f;
    return float3(
        chromaBase + color.y * 0.5f,
        color.x + color.z * 0.5f,
        chromaBase - color.y * 0.5f);
}

float3 EncodeTemporalColor(float3 color, float exposure)
{
    float3 exposed = max(color, 0.0f) * max(exposure, 1e-4f);
    return exposed / (1.0f + Luminance(exposed));
}

float3 DecodeTemporalColor(float3 encoded, float exposure)
{
    float3 exposed = max(
        encoded / max(1.0f - Luminance(encoded), 1e-4f),
        0.0f);
    return exposed / max(exposure, 1e-4f);
}

int2 ClampPixel(int2 pixel)
{
    int2 size = max(int2(gScreenParams.xy), int2(1, 1));
    return clamp(pixel, int2(0, 0), size - 1);
}

float4 LoadCurrent(int2 pixel)
{
    return gCurrentColor.Load(int3(ClampPixel(pixel), 0));
}

float LoadDepth(int2 pixel)
{
    return gSceneDepth.Load(int3(ClampPixel(pixel), 0));
}

float LoadMask(Texture2D<float> mask, int2 pixel)
{
    return mask.Load(int3(ClampPixel(pixel), 0));
}

float CatmullRomWeight(float value)
{
    float x = abs(value);
    if (x <= 1.0f)
    {
        return ((1.5f * x - 2.5f) * x) * x + 1.0f;
    }
    if (x < 2.0f)
    {
        return (((-0.5f * x + 2.5f) * x - 4.0f) * x) + 2.0f;
    }
    return 0.0f;
}

float4 SampleHistoryCatmullRom(float2 uv)
{
    float2 texel = uv * gScreenParams.xy - 0.5f;
    int2 basePixel = int2(floor(texel));
    float2 fraction = texel - float2(basePixel);
    float4 sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        float weightY = CatmullRomWeight(float(y) - fraction.y);
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            float weight = CatmullRomWeight(float(x) - fraction.x) * weightY;
            sum += gHistoryColor.Load(int3(ClampPixel(basePixel + int2(x, y)), 0)) * weight;
            weightSum += weight;
        }
    }
    return sum / max(weightSum, 1e-5f);
}

MotionSample LoadDilatedMotion(uint2 pixel)
{
    int2 basePixel = int2(pixel);
    int2 selectedPixel = basePixel;
    float selectedDepth = LoadDepth(basePixel);

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 candidatePixel = basePixel + int2(x, y);
            float candidateDepth = LoadDepth(candidatePixel);
            if (candidateDepth < selectedDepth)
            {
                selectedDepth = candidateDepth;
                selectedPixel = candidatePixel;
            }
        }
    }

    int2 clampedPixel = ClampPixel(selectedPixel);
    float2 motionPayload = gMotionVectors.Load(int3(clampedPixel, 0));
    float2 metadataPayload = gMotionMetadata.Load(int3(clampedPixel, 0));
    MotionSample sample;
    sample.pixels = motionPayload;
    sample.expectedPreviousDepth = metadataPayload.x;
    sample.valid = metadataPayload.y;
    return sample;
}

float ReferenceMotionLength(float2 motionPixels)
{
    // Express motion in 1080p-equivalent pixels so temporal response remains
    // stable when only the render resolution changes.
    return length(motionPixels) * (1080.0f / max(gScreenParams.y, 1.0f));
}

float CurrentDepthSlope(uint2 pixel)
{
    int2 basePixel = int2(pixel);
    float center = LoadDepth(basePixel);
    float slope = 0.0f;
    slope = max(slope, abs(LoadDepth(basePixel + int2(-1, 0)) - center));
    slope = max(slope, abs(LoadDepth(basePixel + int2(1, 0)) - center));
    slope = max(slope, abs(LoadDepth(basePixel + int2(0, -1)) - center));
    slope = max(slope, abs(LoadDepth(basePixel + int2(0, 1)) - center));
    return slope;
}

float3 ClipHistoryToNeighborhood(
    uint2 pixel,
    float3 historyEncoded,
    float exposure)
{
    int2 basePixel = int2(pixel);
    float3 mean = 0.0f;
    float3 meanSquared = 0.0f;
    float3 minimum = float3(65504.0f, 65504.0f, 65504.0f);
    float3 maximum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 encoded = EncodeTemporalColor(
                LoadCurrent(basePixel + int2(x, y)).rgb,
                exposure);
            float3 sample = RGBToYCoCg(encoded);
            mean += sample;
            meanSquared += sample * sample;
            minimum = min(minimum, sample);
            maximum = max(maximum, sample);
        }
    }

    mean *= 1.0f / 9.0f;
    meanSquared *= 1.0f / 9.0f;
    float3 sigma = sqrt(max(meanSquared - mean * mean, 0.0f));
    float3 lower = max(minimum, mean - sigma * max(gTaaParams.w, 0.0f));
    float3 upper = min(maximum, mean + sigma * max(gTaaParams.w, 0.0f));
    return max(YCoCgToRGB(clamp(RGBToYCoCg(historyEncoded), lower, upper)), 0.0f);
}

float3 SpatialFallback(uint2 pixel, float exposure)
{
    int2 basePixel = int2(pixel);
    float centerDepth = LoadDepth(basePixel);
    float3 center = EncodeTemporalColor(LoadCurrent(basePixel).rgb, exposure);
    float3 sum = center * 4.0f;
    float weightSum = 4.0f;
    const int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };

    [unroll]
    for (uint index = 0; index < 4u; ++index)
    {
        int2 samplePixel = basePixel + offsets[index];
        float depthDelta = abs(LoadDepth(samplePixel) - centerDepth);
        float depthWeight = rcp(1.0f + depthDelta * 2048.0f);
        float3 sample = EncodeTemporalColor(LoadCurrent(samplePixel).rgb, exposure);
        float lumaWeight = rcp(1.0f + abs(Luminance(sample) - Luminance(center)) * 12.0f);
        float weight = depthWeight * lumaWeight;
        sum += sample * weight;
        weightSum += weight;
    }
    return sum / max(weightSum, 1e-5f);
}

RejectionResult EvaluateHistory(
    uint2 pixel,
    float2 previousUv,
    MotionSample motion,
    float3 currentEncoded,
    float3 historyEncoded)
{
    int2 previousPixel = ClampPixel(int2(previousUv * gScreenParams.xy));
    float previousDepth = gHistoryDepth.Load(int3(previousPixel, 0));
    float depthThreshold = max(
        gRejectParams.x,
        CurrentDepthSlope(pixel) * 1.5f + 1e-5f);
    float depthError = abs(previousDepth - motion.expectedPreviousDepth);
    float depthReject = saturate(
        (depthError - depthThreshold) / max(depthThreshold * 3.0f, 1e-5f));
    depthReject = max(depthReject, 1.0f - saturate(motion.valid));

    float currentLuma = Luminance(currentEncoded);
    float historyLuma = Luminance(historyEncoded);
    float relativeLumaError =
        abs(currentLuma - historyLuma) /
        max(max(currentLuma, historyLuma), 0.1f);
    float lumaReject = saturate(
        (relativeLumaError - gRejectParams.y * 0.35f) /
        max(gRejectParams.y, 0.05f));

    float reactive = LoadMask(gReactiveMask, int2(pixel));
    float transparency = LoadMask(gTransparencyMask, int2(pixel));
    float invalidDepthMotion =
        LoadMask(gInvalidDepthMotionMask, int2(pixel));
    RejectionResult result;
    result.depth = depthReject;
    result.luma = lumaReject;
    result.confidence = (1.0f - depthReject) * (1.0f - lumaReject * 0.65f);
    result.confidence *= 1.0f - reactive * 0.90f;
    result.confidence *= 1.0f - transparency * 0.70f;
    result.confidence *= 1.0f - invalidDepthMotion * 0.98f;
    result.confidence = saturate(result.confidence);
    return result;
}

float ResolveHistoryWeight(
    float baseWeight,
    float confidence,
    float2 motionPixels,
    float transparency)
{
    float stationary =
        1.0f - smoothstep(0.35f, 2.5f, ReferenceMotionLength(motionPixels));
    float weight = lerp(baseWeight, min(baseWeight + 0.035f, 0.965f), stationary);
    weight = min(weight, lerp(0.965f, 0.72f, transparency));
    return saturate(weight) * confidence;
}

float3 ApplyStableSharpen(
    uint2 pixel,
    float3 resolved,
    float exposure,
    float confidence,
    float2 motionPixels)
{
    float stable = confidence *
        (1.0f - smoothstep(0.5f, 4.0f, ReferenceMotionLength(motionPixels)));
    float strength = saturate(gRejectParams.z) * stable * 0.35f;
    if (strength <= 0.0f)
    {
        return resolved;
    }

    int2 basePixel = int2(pixel);
    float3 center = EncodeTemporalColor(LoadCurrent(basePixel).rgb, exposure);
    float3 crossAverage =
        (EncodeTemporalColor(LoadCurrent(basePixel + int2(-1, 0)).rgb, exposure) +
         EncodeTemporalColor(LoadCurrent(basePixel + int2(1, 0)).rgb, exposure) +
         EncodeTemporalColor(LoadCurrent(basePixel + int2(0, -1)).rgb, exposure) +
         EncodeTemporalColor(LoadCurrent(basePixel + int2(0, 1)).rgb, exposure)) * 0.25f;
    return max(resolved + (center - crossAverage) * strength, 0.0f);
}

float3 BuildDebugColor(
    MotionSample motion,
    float historyWeight,
    RejectionResult rejection,
    float reactive,
    float transparency,
    float invalidDepthMotion)
{
    uint mode = (uint)round(gRejectParams.w);
    if (mode == 13u)
    {
        float magnitude = saturate(length(motion.pixels) / 16.0f);
        return float3(
            saturate(0.5f + motion.pixels.x / 32.0f),
            saturate(0.5f - motion.pixels.y / 32.0f),
            magnitude);
    }
    if (mode == 14u)
    {
        return historyWeight.xxx;
    }
    if (mode == 15u)
    {
        return float3(rejection.depth, rejection.luma, 0.0f);
    }
    if (mode == 16u)
    {
        return float3(reactive, 0.0f, 0.0f);
    }
    if (mode == 17u)
    {
        return float3(0.0f, transparency, transparency);
    }
    if (mode == 18u)
    {
        return (1.0f - rejection.confidence).xxx;
    }
    if (mode == 19u)
    {
        return float3(invalidDepthMotion, invalidDepthMotion * 0.5f, 0.0f);
    }
    return 0.0f;
}

PSOut PSMain(VSOut input)
{
    uint2 size = max(uint2(gScreenParams.xy), uint2(1u, 1u));
    uint2 pixel = min(uint2(input.position.xy), size - 1u);
    float4 current = gCurrentColor.Load(int3(pixel, 0));
    float currentDepth = LoadDepth(int2(pixel));
    float currentExposure =
        gTaaParams.z >= 0.5f ? max(gExposure.Load(int3(0, 0, 0)), 1e-4f) : 1.0f;
    float3 currentEncoded = EncodeTemporalColor(current.rgb, currentExposure);
    MotionSample motion = LoadDilatedMotion(pixel);
    float reactive = LoadMask(gReactiveMask, int2(pixel));
    float transparency = LoadMask(gTransparencyMask, int2(pixel));
    float invalidDepthMotion =
        LoadMask(gInvalidDepthMotionMask, int2(pixel));

    PSOut output;
    output.historyColor = float4(currentEncoded, currentExposure);
    output.historyDepth = currentDepth;
    output.resolvedColor = current;

    RejectionResult rejection;
    rejection.depth = 1.0f;
    rejection.luma = 0.0f;
    rejection.confidence = 0.0f;
    float historyWeight = 0.0f;

    if (gTaaParams.x >= 0.5f && motion.valid >= 0.5f)
    {
        float2 previousUv = input.uv + motion.pixels * gScreenParams.zw;
        if (all(previousUv >= 0.0f) && all(previousUv <= 1.0f))
        {
            float4 historySample = SampleHistoryCatmullRom(previousUv);
            float historyExposure = max(historySample.a, 1e-4f);
            float3 historyLinear = DecodeTemporalColor(historySample.rgb, historyExposure);
            float3 historyEncoded = EncodeTemporalColor(historyLinear, currentExposure);
            historyEncoded = ClipHistoryToNeighborhood(
                pixel,
                historyEncoded,
                currentExposure);
            rejection = EvaluateHistory(
                pixel,
                previousUv,
                motion,
                currentEncoded,
                historyEncoded);
            historyWeight = ResolveHistoryWeight(
                saturate(gTaaParams.y),
                rejection.confidence,
                motion.pixels,
                transparency);

            float fallbackBlend =
                saturate((1.0f - rejection.confidence) * 0.12f + rejection.depth * 0.08f);
            float3 currentResolve = lerp(
                currentEncoded,
                SpatialFallback(pixel, currentExposure),
                fallbackBlend);
            float3 resolvedEncoded = lerp(currentResolve, historyEncoded, historyWeight);
            resolvedEncoded = ApplyStableSharpen(
                pixel,
                resolvedEncoded,
                currentExposure,
                rejection.confidence,
                motion.pixels);
            output.historyColor = float4(resolvedEncoded, currentExposure);
            output.resolvedColor = float4(
                DecodeTemporalColor(resolvedEncoded, currentExposure),
                current.a);
        }
    }

    output.debugColor = float4(
        BuildDebugColor(
            motion,
            historyWeight,
            rejection,
            reactive,
            transparency,
            invalidDepthMotion),
        1.0f);
    return output;
}
