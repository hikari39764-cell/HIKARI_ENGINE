#include "Include/Surface/HIKARI_ScreenSpaceReflectionTrace.hlsli"

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

    const float3 incident =
        normalize(worldPosition - gCameraPosition.xyz);
    const float viewNormal = saturate(dot(-incident, normal));
    const float dielectricFresnel =
        0.04f + 0.96f * pow(1.0f - viewNormal, 5.0f);
    const float reflectivity =
        saturate(lerp(dielectricFresnel, 0.85f, metallic));
    const float replacementWeight = saturate(
        trace.confidence * reflectivity * gIntensity);
    return float4(trace.radiance, replacementWeight);
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

    const float replacementWeight = saturate(trace.a);
    if (replacementWeight <= 0.0001f)
    {
        return baseSceneColor;
    }

    const float3 worldPosition =
        ReconstructWorldPosition(input.uv, depth);
    const float3 incident =
        normalize(worldPosition - gCameraPosition.xyz);
    const float viewNormal = saturate(dot(-incident, normal));
    const float dielectricFresnel =
        0.04f + 0.96f * pow(1.0f - viewNormal, 5.0f);
    const float reflectivity =
        saturate(lerp(dielectricFresnel, 0.85f, metallic));

    // Forward は既に Probe/Sky の反射を含むため、SSR は画面内で確認できた
    // 信頼度分だけ既存の近似を置換し、加算による二重反射を避ける。
    const float3 estimatedFallback =
        baseSceneColor.rgb * reflectivity * 0.25f;
    const float3 resolved =
        max(
            baseSceneColor.rgb -
                estimatedFallback * replacementWeight,
            0.0f) +
        trace.rgb * replacementWeight;
    return float4(resolved, baseSceneColor.a);
}
