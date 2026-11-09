// 地形モジュール（TerrainKit）の河と湖の水面。帯を不透明の後に場面色へ混ぜる（深度へは書かない）。
// 河の波は曲線の座標（横の距離、流れの距離）で作るので、曲がっても流れの向きに進む（流れの図が要らない）。
// 湖は範囲を切った帯なので波は世界座標で作る。反射は引擎の水面と同じ光学の部品（フレネル、画面空間の追跡、空）を使う。
// root は Render3D_TerrainKitSurface と同じ。
#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#include "Include/Forward/HIKARI_AmbientIblCommon.hlsli"
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "Include/Forward/HIKARI_ContactShadow.hlsli"
#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceReflectionTrace.hlsli"
#include "Include/Surface/HIKARI_SurfaceTransmission.hlsli"
#include "Include/Surface/HIKARI_WaterSpectralSlope.hlsli"
#include "Include/Water/HIKARI_WaterOptics.hlsli"

struct RiverCrossSection
{
    float3 center;
    float left;
    float right;
    float2 tangent;
    float along;
};

struct RiverSection
{
    RiverCrossSection a;
    RiverCrossSection b;
};

StructuredBuffer<RiverSection> gRiverSections : register(t0, space10);

cbuffer RenderSurfaceConstants : register(b3)
{
    float4x4 gSurfaceViewProj;
    float3 gSurfaceCameraPosition;
    uint gSurfacePass;
    float3 gRiverColor;
    float gRiverClarity;
    float gRiverFlowSpeed;
    float gRiverAcrossCells;
    float gRiverWorldAligned;
    float gRiverReserved;
};

struct RiverVertex
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float across : TEXCOORD1;
    float along : TEXCOORD2;
    float2 tangent : TEXCOORD3;
    float steepness : TEXCOORD4;
};

RiverVertex SurfaceVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const RiverSection section = gRiverSections[instanceId];
    const uint columns = (uint)gRiverAcrossCells + 1u;
    const uint column = vertexId % columns;
    const bool second = vertexId >= columns;
    RiverCrossSection slice = section.a;
    if (second)
    {
        slice = section.b;
    }
    const float side = (float)column / gRiverAcrossCells * 2.0f - 1.0f;
    const float offset = side * (side < 0.0f ? slice.left : slice.right);
    const float2 lateral = float2(slice.tangent.y, -slice.tangent.x);

    RiverVertex output;
    output.worldPosition = slice.center + float3(lateral.x, 0.0f, lateral.y) * offset;
    output.position = mul(gSurfaceViewProj, float4(output.worldPosition, 1.0f));
    output.across = gRiverWorldAligned > 0.5f ? output.worldPosition.x : offset;
    output.along = gRiverWorldAligned > 0.5f ? output.worldPosition.z : slice.along;
    output.tangent = slice.tangent;
    const float run = max(section.b.along - section.a.along, 0.01f);
    output.steepness = saturate((section.a.center.y - section.b.center.y) / run * 5.0f);
    return output;
}

float RiverHash(int2 cell)
{
    uint h = (uint)cell.x * 1597334677u ^ (uint)cell.y * 3812015801u;
    h *= 3812015801u;
    h ^= h >> 16u;
    h *= 1597334677u;
    return (float)(h >> 8u) / 16777215.0f;
}

// 値噪声とその勾配（x = 値、yz = 勾配）。
float3 RiverNoise(float2 position)
{
    const float2 base = floor(position);
    const float2 f = position - base;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float2 du = 6.0f * f * (1.0f - f);
    const int2 cell = int2(base);
    const float a = RiverHash(cell);
    const float b = RiverHash(cell + int2(1, 0));
    const float c = RiverHash(cell + int2(0, 1));
    const float d = RiverHash(cell + int2(1, 1));
    const float k = a - b - c + d;
    return float3(a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y,
                  du.x * ((b - a) + k * u.y),
                  du.y * ((c - a) + k * u.x));
}

// 流れの座標の波。流れの向きに伸ばし、波長ごとに少しずつ違う速さで流す。
float2 RiverSlope(float across, float along, float seconds, out float pattern)
{
    static const float kWavelengths[3] = {3.1f, 1.15f, 0.43f};
    static const float kAmplitudes[3] = {0.075f, 0.03f, 0.011f};
    static const float kSpeeds[3] = {0.82f, 1.0f, 1.21f};
    float2 slope = 0.0f.xx;
    pattern = 0.0f;
    for (uint octave = 0u; octave < 3u; ++octave)
    {
        const float frequency = 1.0f / kWavelengths[octave];
        const float2 position = float2(across * frequency + (float)octave * 7.3f,
                                       (along - seconds * gRiverFlowSpeed * kSpeeds[octave]) * frequency * 0.55f);
        const float3 noise = RiverNoise(position);
        slope += noise.yz * float2(frequency, frequency * 0.55f) * kAmplitudes[octave];
        pattern += noise.x * (octave == 0u ? 0.5f : 0.25f);
    }
    return slope;
}

float4 SurfaceDepthAwarePS(RiverVertex input) : SV_TARGET
{
    const float3 worldPosition = input.worldPosition;
    const float3 toSurface = worldPosition - gCameraPos.xyz;
    const float viewDistance = length(toSurface);
    const float3 view = -toSurface / max(viewDistance, 1.0e-3f);

    uint width;
    uint height;
    gSceneDepthTex.GetDimensions(width, height);
    const float2 screenUv = input.position.xy / float2(width, height);
    float3 receiver;
    if (!HikariReconstructWorldPosition(screenUv, gSceneDepthTex.Load(int3(int2(input.position.xy), 0)).r,
                                        gInvViewProj, receiver))
    {
        receiver = worldPosition - float3(0.0f, 64.0f, 0.0f);
    }
    const float thickness = max(dot(receiver - worldPosition, -view), 0.0f);
    const float verticalDepth = max(worldPosition.y - receiver.y, 0.0f);

    float pattern;
    const float2 slope = RiverSlope(input.across, input.along, gTimeParams.x, pattern);
    const float2 tangent = normalize(input.tangent);
    const float2 lateral = float2(tangent.y, -tangent.x);
    const float strength = (1.0f + input.steepness * 2.5f) / (1.0f + viewDistance / 180.0f);
    const float2 worldSlope = (lateral * slope.x + tangent * slope.y) * strength;
    const float3 normal = normalize(float3(-worldSlope.x, 1.0f, -worldSlope.y));
    const float3 up = float3(0.0f, 1.0f, 0.0f);

    const float3 light = normalize(-gDirectionalDir.xyz);
    const float shadowVisibility = SampleDirectionalVisibility(worldPosition, up, 1u);
    const HikariPbrShadingContext pbr =
        HikariBuildForwardPbrShadingContext(1.0f.xxx, 0.0f, 0.07f, 1.0f.xxx, 1.0f, normal, view);
    const HikariAmbientIblLobes ambient =
        HikariEvaluateAmbientIblLobes(pbr, 1.0f, float4(0.0f, 0.0f, 0.0f, 1.0f), worldPosition, up, 0.0f.xxxx, 0.0f);
    const float3 sunRadiance =
        gDirectionalColor.rgb * (gDirectionalIntensity * HikariForwardCloudSolarVisibility(worldPosition));
    HikariDirectPbrLobes directional = HikariEvaluateDirectionalPbrLobes(pbr, light, sunRadiance, gDirectionalDir.w);
    directional.diffuse *= shadowVisibility;
    directional.specular *= shadowVisibility;
    const float3 irradiance = directional.diffuse + ambient.diffuse;

    // 反射：画面に映っていれば追跡の結果、外れたら環境（引擎の水面と同じ優先順）。
    const float cosine = dot(normal, view);
    const float fresnel = HikariWaterResolveFresnel(cosine, true);
    const float3 reflectionDirection = HikariSafeNormalize(reflect(-view, normal), normal);
    const float3 environment = HikariWaterResolveEnvironmentFallback(
        ambient.specular, reflectionDirection, cosine, fresnel, 0.07f, 1.0f, gRiverColor);
    HikariScreenSpaceReflectionTraceConfig trace;
    trace.valid = (gScreenSpaceTraceFlags & HIKARI_SCREEN_SPACE_DEPTH_TRACE_VALID) != 0u &&
        gScreenSpaceReflectionParams0.x > 0.0f ? 1u : 0u;
    const float globalSteps = max(HikariFiniteOr(gScreenSpaceReflectionParams1.z, 0.0f), 0.0f);
    trace.maxSteps = (uint)ceil(globalSteps * 0.5f);
    trace.hzbMipCount = max(gScreenSpaceDepthPyramidMipCount, 1u);
    trace.maxDistance = HikariFiniteOr(gScreenSpaceReflectionParams0.y, 0.0f);
    trace.thickness = HikariFiniteOr(gScreenSpaceReflectionParams0.z, 0.0f);
    trace.maxRoughness = HikariFiniteOr(gScreenSpaceReflectionParams0.w, 0.0f);
    trace.stride = HikariFiniteOr(gScreenSpaceReflectionParams1.x, 1.0f) * (globalSteps / max((float)trace.maxSteps, 1.0f));
    trace.edgeFade = HikariFiniteOr(gScreenSpaceReflectionParams1.y, 0.0f);
    HikariScreenSpaceReflectionTraceResult traced = HikariTraceScreenSpaceReflectionFromSurface(
        gSceneColorTex, gSceneDepthTex, gScreenSpaceDepthPyramid, gScreenSpacePackedSurface, gLinearClamp, gPointClamp,
        worldPosition, normal, 0.07f, gCameraPos.xyz, gViewProj, gInvViewProj, trace);
    if (traced.hit != 0u && HikariIsValidScreenSpaceReflectionRadiance(traced.radiance))
    {
        traced.radiance = HikariRemovePreExposure(traced.radiance, gViewExposure);
    }
    else
    {
        traced.hit = 0u;
    }
    const float3 reflection = HikariWaterResolveSsrReplacement(
        environment, traced, fresnel, max(HikariFiniteOr(gScreenSpaceReflectionParams0.x, 0.0f), 0.0f));

    // 水の厚さで奥が見えなくなる。岸では厚さが 0 へ落ちて境目が消える。
    const float body = 1.0f - exp(-thickness / max(gRiverClarity, 0.01f));
    const float edge = saturate(verticalDepth / 0.05f);
    // 泡：岸の浅い所と急な所。模様で崩す。
    const float shallowFoam = saturate(1.0f - verticalDepth / 0.22f) * smoothstep(0.45f, 0.72f, pattern);
    const float rapidFoam = input.steepness * smoothstep(0.42f, 0.66f, pattern);
    const float foam = saturate(shallowFoam * 0.6f + rapidFoam * 0.8f);
    const float alpha = saturate(max(body, foam)) * edge;
    const float3 color = irradiance * (gRiverColor * body + foam.xxx * 0.8f) * edge +
        (directional.specular + reflection) * edge;
    return HikariPreExposeView(float4(ApplyFog(color, worldPosition), alpha));
}
