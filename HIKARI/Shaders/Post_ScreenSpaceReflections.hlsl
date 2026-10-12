#include "Include/Surface/HIKARI_ScreenSpaceReflectionTrace.hlsli"
#include "Include/Surface/HIKARI_SceneReflectionContribution.hlsli"
#include "Include/Temporal/HIKARI_TemporalReprojection.hlsli"

cbuffer ScreenSpaceReflectionCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;
    float4 gTraceScreenParams;
    float4 gTraceParams;
    float4 gResolveParams;
    float4 gFrameParams; // x debug mode, y P/P_prev, z history used, w history weight
    float4x4 gPreviousInvViewProj;
    float4 gPreviousCameraPosition;
    float4 gJitterPixels;
};

#define gMaxDistance gTraceParams.x
#define gThickness gTraceParams.y
#define gIntensity gTraceParams.z
#define gMaxRoughness gTraceParams.w
#define gStride gResolveParams.x
#define gMaxSteps ((uint)gResolveParams.y)
#define gHzbMipCount gResolveParams.z
#define gEdgeFade gResolveParams.w
#define gDebugMode ((uint)gFrameParams.x)

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneSurface : register(t2);
Texture2D<float> gCurrentHzb : register(t3);
Texture2D<float4> gSsrTrace : register(t4);
Texture2D<float4> gReflectionFallback : register(t5);
Texture2D<float4> gReflectionResponse : register(t6);
Texture2D<float4> gReflectionHistory : register(t7);
Texture2D<float4> gReflectionHistoryValidation : register(t8);
Texture2D<float2> gMotionVectors : register(t9);
Texture2D<float2> gMotionMetadata : register(t10);
Texture2D<float2> gTraceHit : register(t11); // 距離、命中面の運動 [画素]
Texture2D<float2> gHistoryHit : register(t12);
SamplerState gLinearClamp : register(s0);
SamplerState gPointClamp : register(s1);

struct FullscreenVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FullscreenVertex VSMain(uint vertexId : SV_VertexID)
{
    FullscreenVertex output;
    const float2 position =
        float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.uv = position;
    output.position =
        float4(
            position * float2(2.0f, -2.0f) +
                float2(-1.0f, 1.0f),
            0.0f,
            1.0f);
    return output;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float3 world;
    HikariReconstructWorldPosition(uv, depth, gInvViewProj, world);
    return world;
}

float ReconstructViewDepth(float2 uv, float depth)
{
    float viewDepth = 0.0f;
    HikariReconstructViewDepth(
        uv,
        depth,
        gInvViewProj,
        viewDepth);
    return viewDepth;
}

struct ReflectionTraceOutput
{
    float4 radiance : SV_TARGET0;
    float2 hit : SV_TARGET1;
};

ReflectionTraceOutput PSTrace(FullscreenVertex input)
{
    ReflectionTraceOutput output = (ReflectionTraceOutput)0;
    if (!HikariHasReflectionContribution(
        gReflectionFallback.SampleLevel(gPointClamp, input.uv, 0),
        gReflectionResponse.SampleLevel(gPointClamp, input.uv, 0))) return output;
    const float depth =
        gSceneDepth.SampleLevel(gPointClamp, input.uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
    {
        return output;
    }

    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(
        gSceneSurface.SampleLevel(gPointClamp, input.uv, 0.0f),
        normal,
        roughness,
        metallic);

    HikariScreenSpaceReflectionTraceConfig config;
    config.valid = 1u;
    config.maxSteps = gMaxSteps;
    config.hzbMipCount = (uint)max(gHzbMipCount, 1.0f);
    config.maxDistance = gMaxDistance;
    config.thickness = gThickness;
    config.maxRoughness = gMaxRoughness;
    config.stride = gStride;
    config.edgeFade = gEdgeFade;

    const float3 worldPosition =
        ReconstructWorldPosition(input.uv, depth);
    const HikariScreenSpaceReflectionTraceResult trace =
        HikariTraceScreenSpaceReflectionFromSurface(
            gSceneColor,
            gSceneDepth,
            gCurrentHzb,
            gSceneSurface,
            gLinearClamp,
            gPointClamp,
            worldPosition,
            normal,
            roughness,
            gCameraPosition.xyz,
            gViewProj,
            gInvViewProj,
            config);

    if (gDebugMode == 1u)
    {
        output.radiance = float4(trace.confidence.xxx, trace.confidence);
        return output;
    }
    if (gDebugMode == 2u)
    {
        output.radiance = float4(
            trace.hit != 0u ? trace.hitUv : 0.0f.xx,
            trace.hit != 0u ? 1.0f : 0.0f,
            1.0f);
        return output;
    }
    if (trace.hit == 0u || trace.confidence <= 0.0001f)
    {
        return output;
    }

    output.radiance = float4(trace.radiance, saturate(trace.confidence));
    const float hitDepth = gSceneDepth.SampleLevel(gPointClamp, trace.hitUv, 0);
    const float3 hitPosition = ReconstructWorldPosition(trace.hitUv, hitDepth);
    output.hit = float2(min(length(hitPosition - worldPosition), 65000.0f),
        length(gMotionVectors.SampleLevel(gPointClamp, trace.hitUv, 0)));
    return output;
}

float4 ResolveHalfResolutionTrace(
    float2 uv,
    float centerDepth,
    float3 centerNormal,
    float centerRoughness)
{
    const float2 tracePosition =
        uv * gTraceScreenParams.xy - 0.5f;
    const int2 basePixel = int2(floor(tracePosition));
    const float2 fraction = frac(tracePosition);
    const int2 maximumPixel =
        int2(gTraceScreenParams.xy) - 1;
    const float centerViewDepth =
        ReconstructViewDepth(uv, centerDepth);

    float4 accumulated = 0.0f.xxxx;
    float accumulatedWeight = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            const int2 tracePixel =
                clamp(
                    basePixel + int2(x, y),
                    int2(0, 0),
                    maximumPixel);
            const float2 sampleUv =
                (float2(tracePixel) + 0.5f) *
                gTraceScreenParams.zw;
            const float sampleDepth =
                gSceneDepth.SampleLevel(
                    gPointClamp,
                    sampleUv,
                    0.0f);
            if (!HikariIsValidSceneDepth(sampleDepth))
            {
                continue;
            }

            float3 sampleNormal;
            float sampleRoughness;
            float sampleMetallic;
            HikariUnpackSceneSurface(
                gSceneSurface.SampleLevel(
                    gPointClamp,
                    sampleUv,
                    0.0f),
                sampleNormal,
                sampleRoughness,
                sampleMetallic);
            const float sampleViewDepth =
                ReconstructViewDepth(sampleUv, sampleDepth);
            const float relativeDepthDifference =
                abs(sampleViewDepth - centerViewDepth) /
                max(centerViewDepth * 0.02f, 0.05f);
            const float depthWeight =
                pow(
                    saturate(1.0f - relativeDepthDifference),
                    2.0f);
            const float normalWeight =
                pow(saturate(dot(centerNormal, sampleNormal)), 12.0f);
            const float roughnessWeight =
                pow(saturate(1.0f - abs(centerRoughness - sampleRoughness) * 8.0f), 2.0f);
            const float2 axisWeight =
                float2(
                    x == 0 ? 1.0f - fraction.x : fraction.x,
                    y == 0 ? 1.0f - fraction.y : fraction.y);
            const float weight =
                axisWeight.x *
                axisWeight.y *
                depthWeight *
                normalWeight *
                roughnessWeight;
            if (!HikariIsFiniteFloat(weight) || weight <= 0.0f)
            {
                continue;
            }
            // 未命中・無効な trace も表面の支持領域に残し、信頼度を水増ししない。
            accumulatedWeight += weight;
            const float4 traceSample =
                gSsrTrace.Load(int3(tracePixel, 0));
            if (!HikariIsValidScreenSpaceReflectionRadiance(
                    traceSample.rgb) ||
                !HikariIsFiniteFloat(traceSample.a) ||
                traceSample.a < 0.0f ||
                traceSample.a > 1.0f)
            {
                continue;
            }
            accumulated += float4(traceSample.rgb * traceSample.a, traceSample.a) * weight;
        }
    }

    if (accumulatedWeight <= 1.0e-5f || accumulated.a <= 1.0e-5f)
    {
        return 0.0f.xxxx;
    }
    // 放射輝度と confidence を分離し、合成時の一度だけ減衰させる。
    return float4(accumulated.rgb / accumulated.a,
        saturate(accumulated.a / accumulatedWeight));
}

struct ReflectionHistoryOutput
{
    float4 radiance : SV_TARGET0;
    float4 validation : SV_TARGET1;
    float2 hit : SV_TARGET2;
};

// trace と履歴の標本。a は確信度で、0 は外れた光線（環境 fallback に任せる分）として平均に数える。
bool IsValidReflectionSample(float4 sample)
{
    return HikariIsValidScreenSpaceReflectionRadiance(sample.rgb) &&
        isfinite(sample.a) && sample.a >= 0.0f && sample.a <= 1.0f;
}

// 反射の降噪。画素ごと一本の光線（粗い面ほど向きが散る）を同じ面の近傍で空間に平均し（粗さに応じた幅、鏡に近い面は
// 中心だけ）、表面の動きで再投影した履歴と混ぜる。放射輝度は確信度で重みづけて平均するので、一本外れても履歴を捨てない。
// 明るい標本は輝度で重みを下げ（Karis の平均）、太陽の映り込みの一点が斑に散らないようにする。履歴は近傍の平均 ±
// 標準偏差へ制限して、遮蔽境界や照明の変化を引きずらない。
ReflectionHistoryOutput PSHistory(FullscreenVertex input)
{
    ReflectionHistoryOutput output = (ReflectionHistoryOutput)0;
    const int2 pixel = min(int2(input.position.xy), int2(gTraceScreenParams.xy) - 1);
    const float depth = gSceneDepth.SampleLevel(gPointClamp, input.uv, 0);
    if (!HikariIsValidSceneDepth(depth)) return output;
    const float4 packed = gSceneSurface.SampleLevel(gPointClamp, input.uv, 0);
    const float3 normal = HikariDecodeOctahedralDirection(packed.xy);
    const float3 world = ReconstructWorldPosition(input.uv, depth);
    const float distanceToCamera = length(world - gCameraPosition.xyz);
    if (!all(isfinite(packed)) || !isfinite(distanceToCamera) || distanceToCamera >= 65500.0f) return output;
    output.validation = float4(packed.xyz, distanceToCamera);

    // 空間の幅（trace の texel の標準偏差）は粗さで決める。粗さ 0.05 以下は鏡の向きだけなので中心だけ、0.4 以上で 2 texel。
    const float blur = saturate((packed.z - 0.05f) / 0.35f);
    const float falloff = 0.5f / max(4.0f * blur * blur, 0.1f);
    // 同じ面とみなす接平面からの距離（距離の 1.5% + 1cm）。
    const float planeTolerance = distanceToCamera * 0.015f + 0.01f;
    float support = 0.0f;
    float hitDistanceSum = 0.0f, hitDistanceSquare = 0.0f, hitDistanceWeight = 0.0f, hitMotion = 0.0f;
    float confidenceSum = 0.0f;
    float3 radianceSum = 0.0f.xxx;
    float radianceWeight = 0.0f;
    float3 moment1 = 0.0f.xxx;
    float3 moment2 = 0.0f.xxx;
    float momentWeight = 0.0f;
    float momentWeightSquare = 0.0f;
    uint momentSamples = 0u;
    [unroll] for (int y = -2; y <= 2; ++y) [unroll] for (int x = -2; x <= 2; ++x)
    {
        const float spatial = exp(-falloff * float(x * x + y * y));
        // 確率的な光線の分散は色の blur 幅と別に、同じ面の 5x5 標本で測る。
        // 狭い空間重みを統計にも使うと実効標本数が 1 に近づき、収束した履歴を毎フレームの雑音へ潰してしまう。
        const bool varianceNeighbor = packed.z > 0.05f || (abs(x) <= 1 && abs(y) <= 1);
        [branch] if (spatial >= 0.01f || varianceNeighbor)
        {
            const int2 neighbor = clamp(pixel + int2(x, y), 0, int2(gTraceScreenParams.xy) - 1);
            const float2 uv = (float2(neighbor) + 0.5f) * gTraceScreenParams.zw;
            const float sampleDepth = gSceneDepth.SampleLevel(gPointClamp, uv, 0);
            const float4 sampleSurface = gSceneSurface.SampleLevel(gPointClamp, uv, 0);
            const float4 sample = gSsrTrace.Load(int3(neighbor, 0));
            if (HikariIsValidSceneDepth(sampleDepth) && IsValidReflectionSample(sample) &&
                dot(normal, HikariDecodeOctahedralDirection(sampleSurface.xy)) >= 0.9f &&
                abs(sampleSurface.z - packed.z) <= 0.1f &&
                abs(dot(normal, ReconstructWorldPosition(uv, sampleDepth) - world)) <= planeTolerance)
            {
                const float colorWeight = spatial >= 0.01f ? spatial : 0.0f;
                const float hit = sample.a * colorWeight;
                const float karis = hit / (1.0f + dot(sample.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
                support += colorWeight;
                confidenceSum += hit;
                radianceSum += sample.rgb * karis;
                radianceWeight += karis;
                const float2 hitInfo = gTraceHit.Load(int3(neighbor, 0));
                if (hitInfo.x > 0 && all(isfinite(hitInfo)))
                {
                    hitDistanceSum += hitInfo.x * hit;
                    hitDistanceSquare += hitInfo.x * hitInfo.x * hit;
                    hitDistanceWeight += hit;
                    if (colorWeight > 0.01f) hitMotion = max(hitMotion, hitInfo.y);
                }
                const float varianceWeight = varianceNeighbor ? sample.a : 0.0f;
                moment1 += sample.rgb * varianceWeight;
                moment2 += sample.rgb * sample.rgb * varianceWeight;
                momentWeight += varianceWeight;
                momentWeightSquare += varianceWeight * varianceWeight;
                momentSamples += varianceWeight > 0.0f ? 1u : 0u;
            }
        }
    }
    if (support <= 1.0e-5f) return output;
    const float currentConfidence = confidenceSum / support;
    const float3 currentRadiance = radianceWeight > 1.0e-6f ? radianceSum / radianceWeight : 0.0f.xxx;
    output.radiance = float4(currentRadiance, currentConfidence);
    output.hit = float2(hitDistanceSum / max(hitDistanceWeight, 1e-6f), hitMotion);
    if (gFrameParams.z < 0.5f) return output;
    const float2 motion = gMotionVectors.SampleLevel(gPointClamp, input.uv, 0);
    const float2 metadata = gMotionMetadata.SampleLevel(gPointClamp, input.uv, 0);
    if (!all(isfinite(motion)) || !all(isfinite(metadata)) || metadata.y < 0.5f ||
        !HikariIsValidSceneDepth(metadata.x)) return output;
    const float2 previousUv = HikariReprojectHistoryUv(input.uv, motion, gScreenParams.zw,
        gJitterPixels.xy, gJitterPixels.zw);
    if (any(previousUv <= 0) || any(previousUv >= 1)) return output;
    float3 previousWorld;
    if (!HikariReconstructWorldPosition(previousUv, metadata.x, gPreviousInvViewProj, previousWorld)) return output;
    const float expectedDistance = length(previousWorld - gPreviousCameraPosition.xyz);
    if (!isfinite(expectedDistance)) return output;

    const float2 historyPosition = previousUv * gTraceScreenParams.xy - 0.5f;
    const int2 basePixel = int2(floor(historyPosition));
    const float2 fraction = frac(historyPosition);
    float4 historySum = 0;
    float historySupport = 0;
    float previousHitMotion = 0;
    [unroll] for (int hy = 0; hy < 2; ++hy) [unroll] for (int hx = 0; hx < 2; ++hx)
    {
        const int2 historyPixel = basePixel + int2(hx, hy);
        if (any(historyPixel < 0) || any(historyPixel >= int2(gTraceScreenParams.xy))) continue;
        const float4 validation = gReflectionHistoryValidation.Load(int3(historyPixel, 0));
        const float4 history = gReflectionHistory.Load(int3(historyPixel, 0));
        if (!all(isfinite(validation)) || validation.w <= 0 || !IsValidReflectionSample(history)) continue;
        const float3 previousNormal = HikariDecodeOctahedralDirection(validation.xy);
        const float depthWeight = saturate(1.0f - abs(validation.w - expectedDistance) / max(expectedDistance * 0.01f, 0.03f));
        const float normalWeight = saturate((dot(normal, previousNormal) - 0.9f) * 10.0f);
        const float roughnessWeight = saturate(1.0f - abs(packed.z - validation.z) * 10.0f);
        const float2 previousHitInfo = gHistoryHit.Load(int3(historyPixel, 0));
        const float previousHit = previousHitInfo.x;
        // 鏡に近い反射だけ命中距離で幾何を検証する。粗い反射は同一画素でも別の物を標本化するので、
        // 距離による棄却を弱め、色の分散と命中面の動きで応答する。
        const float hitSpread = sqrt(max(hitDistanceSquare / max(hitDistanceWeight, 1e-6f) - output.hit.x * output.hit.x, 0));
        const float hitTolerance = max(0.05f, output.hit.x * 0.03f + hitSpread * 3.0f);
        const float hitWeight = output.hit.x > 0 && previousHit > 0
            ? 1.0f - smoothstep(hitTolerance, hitTolerance * 2.0f, abs(previousHit - output.hit.x)) : 0.0f;
        const float weight = (hx == 0 ? 1 - fraction.x : fraction.x) * (hy == 0 ? 1 - fraction.y : fraction.y) *
            depthWeight * normalWeight * roughnessWeight *
            lerp(1.0f, hitWeight, saturate((0.15f - packed.z) / 0.10f));
        historySum += float4(history.rgb * history.a, history.a) * weight;
        historySupport += weight;
        if (weight > 0.01f && all(isfinite(previousHitInfo)))
            previousHitMotion = max(previousHitMotion,previousHitInfo.y);
    }
    if (historySupport <= 0.01f) return output;
    // 履歴は前フレームの Pre-Exposure 空間。今の空間へ換算してから近傍の範囲へ制限する。
    // 一本の stochastic ray の miss は確率標本。近傍の 0/1 の範囲で履歴を潰すと、静止中も点滅する。
    // 幾何の再投影で有効性を検査し、命中率そのものは時間で平均する。
    const float historyConfidence = saturate(historySum.a / historySupport);
    float3 historyRadiance = historySum.a > 1.0e-4f ? historySum.rgb / historySum.a * gFrameParams.y : currentRadiance;
    const float referenceMotion = max(length(motion), max(hitMotion,previousHitMotion)) * (1080.0f / max(gScreenParams.y, 1.0f));
    if (momentSamples > 1u && momentWeight > 1.0e-5f)
    {
        const float3 mean = moment1 / momentWeight;
        // 重み付きの不偏分散。少数標本の過小評価で履歴を切り詰めない。
        const float correction = momentWeight * momentWeight /
            max(momentWeight * momentWeight - momentWeightSquare, 1.0e-20f);
        const float3 deviation = sqrt(max(moment2 / momentWeight - mean * mean, 0.0f.xxx) * correction);
        const float sigma = lerp(3.0f, 1.5f, saturate(referenceMotion * 0.1f));
        historyRadiance = clamp(historyRadiance, mean - deviation * sigma, mean + deviation * sigma);
    }
    // 鏡に近い面は光線の揺れが無いので履歴を弱め（表面の動きの再投影は映り込みの動きと合わない）、動きでも弱める。
    const float weight = saturate(gFrameParams.w) * saturate(historySupport) *
        lerp(0.5f, 1.0f, saturate(blur * 4.0f)) / (1.0f + referenceMotion * 0.1f);
    const float confidence = lerp(currentConfidence, historyConfidence, weight);
    const float3 premultiplied =
        lerp(currentRadiance * currentConfidence, historyRadiance * historyConfidence, weight);
    output.radiance = float4(confidence > 1.0e-4f ? premultiplied / confidence : 0.0f.xxx, confidence);
    return output;
}

float4 PSResolve(FullscreenVertex input) : SV_TARGET
{
    const float4 baseSceneColor =
        gSceneColor.SampleLevel(gLinearClamp, input.uv, 0.0f);
    const float depth =
        gSceneDepth.SampleLevel(gPointClamp, input.uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
    {
        return baseSceneColor;
    }

    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(
        gSceneSurface.SampleLevel(gPointClamp, input.uv, 0.0f),
        normal,
        roughness,
        metallic);
    if (!HikariIsFiniteFloat(roughness) || roughness >= gMaxRoughness)
    {
        return baseSceneColor;
    }
    const float4 trace =
        ResolveHalfResolutionTrace(input.uv, depth, normal, roughness);
    if (!HikariIsValidScreenSpaceReflectionRadiance(trace.rgb) ||
        !HikariIsFiniteFloat(trace.a))
    {
        return baseSceneColor;
    }
    if (gDebugMode == 1u)
    {
        return float4(trace.aaa, 1.0f);
    }
    if (gDebugMode == 2u)
    {
        return gSsrTrace.SampleLevel(
            gPointClamp,
            input.uv,
            0.0f);
    }

    const float replacementWeight = saturate(trace.a * gIntensity);
    if (replacementWeight <= 0.0001f)
    {
        return baseSceneColor;
    }

    const float4 fallback = gReflectionFallback.SampleLevel(gPointClamp, input.uv, 0);
    const float4 response = gReflectionResponse.SampleLevel(gPointClamp, input.uv, 0);
    if (!HikariHasReflectionContribution(fallback, response)) return baseSceneColor;
    // 同じ Forward ピクセルが保存した環境鏡面成分のみを信頼度の分だけ置換する。
    const float3 resolved = max(0.0f, baseSceneColor.rgb +
        replacementWeight * (trace.rgb * response.rgb - fallback.rgb));
    if (!HikariIsValidScreenSpaceReflectionRadiance(resolved)) return baseSceneColor;
    return float4(resolved, baseSceneColor.a);
}
