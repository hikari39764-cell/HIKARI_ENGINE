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
    float4 gFrameParams;
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

float4 PSTrace(FullscreenVertex input) : SV_TARGET
{
    if (!HikariHasReflectionContribution(
        gReflectionFallback.SampleLevel(gPointClamp, input.uv, 0),
        gReflectionResponse.SampleLevel(gPointClamp, input.uv, 0))) return 0.0f.xxxx;
    const float depth =
        gSceneDepth.SampleLevel(gPointClamp, input.uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
    {
        return 0.0f.xxxx;
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
        return float4(trace.confidence.xxx, trace.confidence);
    }
    if (gDebugMode == 2u)
    {
        return float4(
            trace.hit != 0u ? trace.hitUv : 0.0f.xx,
            trace.hit != 0u ? 1.0f : 0.0f,
            1.0f);
    }
    if (trace.hit == 0u || trace.confidence <= 0.0001f)
    {
        return 0.0f.xxxx;
    }

    return float4(trace.radiance, saturate(trace.confidence));
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
};

bool IsValidReflectionSample(float4 sample)
{
    return HikariIsValidScreenSpaceReflectionRadiance(sample.rgb) &&
        isfinite(sample.a) && sample.a > 0.0001f && sample.a <= 1.0f;
}

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
    const float4 current = gSsrTrace.Load(int3(pixel, 0));
    // 現在の ray が無効なら古い成功を復活させず、その frame から probe/sky へ戻す。
    if (!IsValidReflectionSample(current)) return output;
    output.radiance = current;
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
    [unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x)
    {
        const int2 historyPixel = basePixel + int2(x, y);
        if (any(historyPixel < 0) || any(historyPixel >= int2(gTraceScreenParams.xy))) continue;
        const float4 validation = gReflectionHistoryValidation.Load(int3(historyPixel, 0));
        const float4 history = gReflectionHistory.Load(int3(historyPixel, 0));
        if (!all(isfinite(validation)) || validation.w <= 0 || !IsValidReflectionSample(history)) continue;
        const float3 previousNormal = HikariDecodeOctahedralDirection(validation.xy);
        const float depthWeight = saturate(1.0f - abs(validation.w - expectedDistance) / max(expectedDistance * 0.01f, 0.03f));
        const float normalWeight = saturate((dot(normal, previousNormal) - 0.9f) * 10.0f);
        const float roughnessWeight = saturate(1.0f - abs(packed.z - validation.z) * 10.0f);
        const float weight = (x == 0 ? 1 - fraction.x : fraction.x) * (y == 0 ? 1 - fraction.y : fraction.y) *
            depthWeight * normalWeight * roughnessWeight;
        historySum += float4(history.rgb * history.a, history.a) * weight;
        historySupport += weight;
    }
    if (historySupport <= 0.01f || historySum.a <= 0.0001f) return output;
    float3 historyRadiance = historySum.rgb / historySum.a;
    const float historyConfidence = saturate(historySum.a / historySupport);
    // 現在の同一面の近傍範囲に制限する。遮蔽境界をまたぐ色や照明変更を履歴で引きずらない。
    float3 lower = current.rgb, upper = current.rgb;
    [unroll] for (int y = -1; y <= 1; ++y) [unroll] for (int x = -1; x <= 1; ++x)
    {
        const int2 neighbor = clamp(pixel + int2(x, y), 0, int2(gTraceScreenParams.xy) - 1);
        const float2 uv = (float2(neighbor) + 0.5f) * gTraceScreenParams.zw;
        const float sampleDepth = gSceneDepth.SampleLevel(gPointClamp, uv, 0);
        if (!HikariIsValidSceneDepth(sampleDepth)) continue;
        const float4 sampleSurface = gSceneSurface.SampleLevel(gPointClamp, uv, 0);
        if (dot(normal, HikariDecodeOctahedralDirection(sampleSurface.xy)) < 0.95f ||
            abs(sampleSurface.z - packed.z) > 0.1f) continue;
        const float sampleDistance = length(ReconstructWorldPosition(uv, sampleDepth) - gCameraPosition.xyz);
        if (abs(sampleDistance - distanceToCamera) > max(distanceToCamera * 0.02f, 0.05f)) continue;
        const float4 sample = gSsrTrace.Load(int3(neighbor, 0));
        if (!IsValidReflectionSample(sample)) continue;
        lower = min(lower, sample.rgb); upper = max(upper, sample.rgb);
    }
    historyRadiance = clamp(historyRadiance, lower, upper);
    const float referenceMotion = length(motion) * (1080.0f / max(gScreenParams.y, 1.0f));
    const float weight = saturate(gFrameParams.w) * saturate(historySupport) *
        min(current.a, historyConfidence) / (1.0f + referenceMotion * 0.25f);
    output.radiance = float4(lerp(current.rgb, historyRadiance, weight),
        min(current.a, lerp(current.a, historyConfidence, weight)));
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
