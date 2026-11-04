#define HIKARI_TEMPORAL_TAARESOLVE
#include "Include/Temporal/HIKARI_TemporalShaderInterface.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// PSResolve は出力・履歴・履歴の深度だけを書く（通常）。PSMain は TAA の診断表示の色も書く。
struct PSResolveOut
{
    float4 resolvedColor : SV_TARGET0;
    float4 historyColor : SV_TARGET1;
    float historyDepth : SV_TARGET2;
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
    return exposed / (1.0f + HikariLuminance(exposed));
}

float3 DecodeTemporalColor(float3 encoded, float exposure)
{
    float3 exposed = max(
        encoded / max(1.0f - HikariLuminance(encoded), 1e-4f),
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
    float4 color = gCurrentColor.Load(int3(ClampPixel(pixel), 0));
    // 無限大や NaN は近傍の組み直しで周りに広がり、履歴に入ると消えないので、ここで 0 にする。
    return all(isfinite(color)) ? color : 0.0f;
}

float LoadDepth(int2 pixel)
{
    return gSceneDepth.Load(int3(ClampPixel(pixel), 0));
}

float LoadMask(Texture2D<float> mask, int2 pixel)
{
    return mask.Load(int3(ClampPixel(pixel), 0));
}

// 今のフレームの 3×3 近傍（符号化した色と深度）。組み直し・運動の膨張・深度の勾配・切り詰め・空間の予備が同じ値を
// 使うので一度だけ読む（以前は色を 22 回、深度を 19 回読んでいた）。添字は (y + 1) * 3 + (x + 1)。
struct CurrentNeighborhood
{
    float3 encoded[9];
    float depth[9];
};

CurrentNeighborhood LoadCurrentNeighborhood(uint2 pixel, float exposure)
{
    CurrentNeighborhood neighborhood;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int index = (y + 1) * 3 + (x + 1);
            neighborhood.encoded[index] = EncodeTemporalColor(LoadCurrent(int2(pixel) + int2(x, y)).rgb, exposure);
            neighborhood.depth[index] = LoadDepth(int2(pixel) + int2(x, y));
        }
    }
    return neighborhood;
}

// 十字の近傍（-x, +x, -y, +y）の添字。
static const int kCrossIndices[4] = {3, 5, 1, 7};

// 今のフレームの各画素は、ずらし（jitter）の分だけ動いた画を写している。近傍 3×3 を各標本の本当の位置から
// 画素の中心までの距離で重み付けし、ずらさない中心の値を作る。出力も履歴もずらさない位置で持つので、画が
// ずらしと一緒に毎フレーム揺れない。
float3 ReconstructCurrentAtCenter(CurrentNeighborhood neighborhood)
{
    float3 sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) - gJitterPixels.xy;
            float weight = exp(-2.29f * dot(offset, offset));
            sum += neighborhood.encoded[(y + 1) * 3 + (x + 1)] * weight;
            weightSum += weight;
        }
    }
    return sum / max(weightSum, 1e-5f);
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

// 履歴の 4×4 の texel（前の位置の周り）。Catmull-Rom と鋭さの十字の双一次補間がどちらもこの中に収まるので一度だけ読む
// （以前は 32 回読んでいた）。添字は (y + 1) * 4 + (x + 1)、x と y は -1..2。
struct HistoryFootprint
{
    float4 texels[16];
    float2 fraction;
};

HistoryFootprint LoadHistoryFootprint(float2 uv)
{
    HistoryFootprint footprint;
    float2 texel = uv * gScreenParams.xy - 0.5f;
    int2 basePixel = int2(floor(texel));
    footprint.fraction = texel - float2(basePixel);
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            footprint.texels[(y + 1) * 4 + (x + 1)] =
                gHistoryColor.Load(int3(ClampPixel(basePixel + int2(x, y)), 0));
        }
    }
    return footprint;
}

float4 SampleHistoryCatmullRom(HistoryFootprint footprint)
{
    float4 sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        float weightY = CatmullRomWeight(float(y) - footprint.fraction.y);
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            float weight = CatmullRomWeight(float(x) - footprint.fraction.x) * weightY;
            sum += footprint.texels[(y + 1) * 4 + (x + 1)] * weight;
            weightSum += weight;
        }
    }
    return sum / max(weightSum, 1e-5f);
}

MotionSample LoadDilatedMotion(uint2 pixel, CurrentNeighborhood neighborhood)
{
    int2 basePixel = int2(pixel);
    int2 selectedPixel = basePixel;
    float selectedDepth = neighborhood.depth[4];

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 candidatePixel = basePixel + int2(x, y);
            float candidateDepth = neighborhood.depth[(y + 1) * 3 + (x + 1)];
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

float CurrentDepthSlope(CurrentNeighborhood neighborhood)
{
    float center = neighborhood.depth[4];
    float slope = 0.0f;
    [unroll]
    for (uint index = 0; index < 4u; ++index)
    {
        slope = max(slope, abs(neighborhood.depth[kCrossIndices[index]] - center));
    }
    return slope;
}

float3 ClipHistoryToNeighborhood(
    CurrentNeighborhood neighborhood,
    float3 historyEncoded)
{
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
            float3 sample = RGBToYCoCg(neighborhood.encoded[(y + 1) * 3 + (x + 1)]);
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

// 履歴を捨てた所（現れたばかりの面など）で今のフレームを少しぼかす。中心は組み直した値を使い、周りには
// ずらしを含めた本当の位置からの距離の重みも掛ける（そのままの画素を混ぜると、そこだけずらしと一緒に揺れる）。
float3 SpatialFallback(CurrentNeighborhood neighborhood, float3 center)
{
    float centerDepth = neighborhood.depth[4];
    float3 sum = center * 4.0f;
    float weightSum = 4.0f;
    const int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };

    [unroll]
    for (uint index = 0; index < 4u; ++index)
    {
        float depthDelta = abs(neighborhood.depth[kCrossIndices[index]] - centerDepth);
        float depthWeight = rcp(1.0f + depthDelta * 2048.0f);
        float3 sample = neighborhood.encoded[kCrossIndices[index]];
        float lumaWeight = rcp(1.0f + abs(HikariLuminance(sample) - HikariLuminance(center)) * 12.0f);
        float2 offset = float2(offsets[index]) - gJitterPixels.xy;
        float distanceWeight = exp(-0.8f * dot(offset, offset));
        float weight = depthWeight * lumaWeight * distanceWeight;
        sum += sample * weight;
        weightSum += weight;
    }
    return sum / max(weightSum, 1e-5f);
}

RejectionResult EvaluateHistory(
    float2 previousUv,
    MotionSample motion,
    CurrentNeighborhood neighborhood,
    float3 currentEncoded,
    float3 historyEncoded,
    float reactive,
    float transparency,
    float invalidDepthMotion)
{
    int2 previousPixel = ClampPixel(int2(previousUv * gScreenParams.xy));
    float previousDepth = gHistoryDepth.Load(int3(previousPixel, 0));
    float depthThreshold = max(
        gRejectParams.x,
        CurrentDepthSlope(neighborhood) * 1.5f + 1e-5f);
    float depthError = abs(previousDepth - motion.expectedPreviousDepth);
    float depthReject = saturate(
        (depthError - depthThreshold) / max(depthThreshold * 3.0f, 1e-5f));
    depthReject = max(depthReject, 1.0f - saturate(motion.valid));

    float currentLuma = HikariLuminance(currentEncoded);
    float historyLuma = HikariLuminance(historyEncoded);
    float relativeLumaError =
        abs(currentLuma - historyLuma) /
        max(max(currentLuma, historyLuma), 0.1f);
    float lumaReject = saturate(
        (relativeLumaError - gRejectParams.y * 0.35f) /
        max(gRejectParams.y, 0.05f));
    // 止まった画素では輝度の差で履歴を捨てない。ずらし（jitter）で前景と背景を交互に拾う縁は毎フレーム輝度が
    // 大きく変わり、捨てると縁が今のフレームのぎざぎざのまま点滅する。止まった画素の履歴は近傍の範囲への
    // 切り詰め（ClipHistoryToNeighborhood）で守られている。
    lumaReject *= smoothstep(0.1f, 1.0f, ReferenceMotionLength(motion.pixels));

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

// 履歴の 4×4 の中で、前の位置から offset 画素ずれた点の双一次補間（Load で組む。標本器を使うと DXIL の束縛が増え、
// 着色器の契約と合わなくなる）。
float4 FootprintBilinear(HistoryFootprint footprint, int2 offset)
{
    const int index = (offset.y + 1) * 4 + (offset.x + 1);
    float4 top = lerp(footprint.texels[index], footprint.texels[index + 1], footprint.fraction.x);
    float4 bottom = lerp(footprint.texels[index + 4], footprint.texels[index + 5], footprint.fraction.x);
    return lerp(top, bottom, footprint.fraction.y);
}

// 鋭さの成分は累積済みの履歴（ずらしに依らない）の中心と十字の平均の差から取る。
float3 HistoryDetail(HistoryFootprint footprint, float3 historyCenterEncoded, float currentExposure)
{
    const int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    float3 crossSum = 0.0f;

    [unroll]
    for (uint index = 0; index < 4u; ++index)
    {
        float4 neighbor = FootprintBilinear(footprint, offsets[index]);
        float3 neighborLinear =
            DecodeTemporalColor(neighbor.rgb, max(neighbor.a, 1e-4f)) * gExposureParams.y;
        crossSum += EncodeTemporalColor(neighborLinear, currentExposure);
    }
    return historyCenterEncoded - crossSum * 0.25f;
}

// 表示にだけ鋭さを足す（履歴へ書き戻すと毎フレーム重なって縁が切り詰めの端まで押し出され、ちらつく）。
// 一度きりで以前の重なった分に近づくよう 2 倍する。
float3 ApplyStableSharpen(
    float3 resolved,
    float3 detail,
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
    float3 sharpened = max(resolved + detail * (strength * 2.0f), 0.0f);
    // 符号化した色は明るさ 1 未満でなければ戻せない（1 に近いと戻した値が跳ね上がる）。
    float luma = HikariLuminance(sharpened);
    return luma > 0.995f ? sharpened * (0.995f / luma) : sharpened;
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

PSResolveOut ResolveTemporal(VSOut input, out float4 debugColor)
{
    uint2 size = max(uint2(gScreenParams.xy), uint2(1u, 1u));
    uint2 pixel = min(uint2(input.position.xy), size - 1u);
    float4 current = gCurrentColor.Load(int3(pixel, 0));
    // 履歴は残差露出 E/P で符号化する（Pre-Exposure 後は 1 付近、FP16 でも劣化しない）。
    float currentExposure =
        gTaaParams.z >= 0.5f ? max(gExposure.Load(int3(0, 0, 0)) * gExposureParams.x, 1e-4f) : 1.0f;
    const CurrentNeighborhood neighborhood = LoadCurrentNeighborhood(pixel, currentExposure);
    float3 currentEncoded = ReconstructCurrentAtCenter(neighborhood);
    MotionSample motion = LoadDilatedMotion(pixel, neighborhood);
    float reactive = LoadMask(gReactiveMask, int2(pixel));
    float transparency = LoadMask(gTransparencyMask, int2(pixel));
    float invalidDepthMotion =
        LoadMask(gInvalidDepthMotionMask, int2(pixel));

    PSResolveOut output;
    output.historyColor = float4(currentEncoded, currentExposure);
    output.historyDepth = neighborhood.depth[4];
    output.resolvedColor = float4(DecodeTemporalColor(currentEncoded, currentExposure), current.a);

    RejectionResult rejection;
    rejection.depth = 1.0f;
    rejection.luma = 0.0f;
    rejection.confidence = 0.0f;
    float historyWeight = 0.0f;

    if (gTaaParams.x >= 0.5f && motion.valid >= 0.5f)
    {
        // 履歴はずらさない画素の中心で持つので、運動ベクトル（ずらしを含まない）だけで前の位置を引く。
        float2 previousUv = input.uv + motion.pixels * gScreenParams.zw;
        if (all(previousUv >= 0.0f) && all(previousUv <= 1.0f))
        {
            const HistoryFootprint footprint = LoadHistoryFootprint(previousUv);
            float4 historySample = SampleHistoryCatmullRom(footprint);
            float historyExposure = max(historySample.a, 1e-4f);
            // 前フレームの Pre-Exposure 空間から現在の空間へ。
            float3 historyLinear = DecodeTemporalColor(historySample.rgb, historyExposure) * gExposureParams.y;
            float3 historyEncoded = EncodeTemporalColor(historyLinear, currentExposure);
            float3 historyDetail = HistoryDetail(footprint, historyEncoded, currentExposure);
            historyEncoded = ClipHistoryToNeighborhood(neighborhood, historyEncoded);
            rejection = EvaluateHistory(
                previousUv,
                motion,
                neighborhood,
                currentEncoded,
                historyEncoded,
                reactive,
                transparency,
                invalidDepthMotion);
            historyWeight = ResolveHistoryWeight(
                saturate(gTaaParams.y),
                rejection.confidence,
                motion.pixels,
                transparency);

            float fallbackBlend =
                saturate((1.0f - rejection.confidence) * 0.12f + rejection.depth * 0.08f);
            float3 currentResolve = lerp(
                currentEncoded,
                SpatialFallback(neighborhood, currentEncoded),
                fallbackBlend);
            float3 resolvedEncoded = lerp(currentResolve, historyEncoded, historyWeight);
            output.historyColor = float4(resolvedEncoded, currentExposure);
            float3 displayedEncoded = ApplyStableSharpen(
                resolvedEncoded,
                historyDetail,
                rejection.confidence,
                motion.pixels);
            output.resolvedColor = float4(
                DecodeTemporalColor(displayedEncoded, currentExposure),
                current.a);
        }
    }

    debugColor = float4(
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

PSResolveOut PSResolve(VSOut input)
{
    float4 debugColor;
    return ResolveTemporal(input, debugColor);
}

PSOut PSMain(VSOut input)
{
    PSOut output;
    const PSResolveOut resolved = ResolveTemporal(input, output.debugColor);
    output.resolvedColor = resolved.resolvedColor;
    output.historyColor = resolved.historyColor;
    output.historyDepth = resolved.historyDepth;
    return output;
}
