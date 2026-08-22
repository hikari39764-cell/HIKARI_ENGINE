#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Features/HIKARI_StylizedFeatureMask.hlsli"

cbuffer ScreenFeatureConstants : register(b0)
{
    float4 gScreenParams;
    float4 gEdgeParams;
    float4 gOutlineColor;
    float4 gDirectionalLight;
    float4 gFaceHairParams;
    uint4 gFeatureFlags;
    float4x4 gViewProj;
};

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gPackedSurface : register(t2);
Texture2D<uint2> gSurfaceFeatureMask : register(t3);
Texture2D<float4> gStylizedOutlineStyle : register(t4);
SamplerState gPointClamp : register(s0);

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
    output.position = float4(
        position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f,
        1.0f);
    return output;
}

int2 ClampSurfacePixel(int2 pixel)
{
    return clamp(pixel, int2(0, 0), int2(gScreenParams.xy) - 1);
}

uint2 LoadSurfaceFeature(int2 pixel)
{
    return gSurfaceFeatureMask.Load(int3(ClampSurfacePixel(pixel), 0));
}

bool IsOutlineSurface(uint featureMask)
{
    const bool screenOutline =
        (gFeatureFlags.x & (1u << 1u)) != 0u &&
        (featureMask & HIKARI_STYLIZED_FEATURE_SCREEN_OUTLINE) != 0u;
    const bool geometryFallback =
        gFeatureFlags.y != 0u &&
        (gFeatureFlags.x & (1u << 0u)) != 0u &&
        (featureMask & HIKARI_STYLIZED_FEATURE_GEOMETRY_OUTLINE) != 0u;
    return screenOutline || geometryFallback;
}

float4 ResolveOutlineStyle(int2 pixel)
{
    if (gFeatureFlags.z != 0u)
    {
        return HikariDecodeStylizedOutlineStyle(
            gStylizedOutlineStyle.Load(
                int3(ClampSurfacePixel(pixel), 0)));
    }
    return float4(
        gOutlineColor.rgb,
        clamp(
            gEdgeParams.x,
            HIKARI_STYLIZED_OUTLINE_MIN_PIXELS,
            HIKARI_STYLIZED_OUTLINE_MAX_PIXELS));
}

float ResolveEdge(
    float2 uv,
    float2 offset,
    float centerDepth,
    float3 centerNormal)
{
    const float sampleDepth = gSceneDepth.SampleLevel(
        gPointClamp, saturate(uv + offset), 0.0f);
    const float3 sampleNormal = HikariDecodeOctahedralNormal(
        gPackedSurface.SampleLevel(
            gPointClamp, saturate(uv + offset), 0.0f).rg);
    const float depthEdge = abs(sampleDepth - centerDepth) /
        max(gEdgeParams.y, abs(centerDepth) * gEdgeParams.y);
    const float normalEdge = (1.0f - saturate(
        dot(centerNormal, sampleNormal))) / gEdgeParams.z;
    return max(depthEdge, normalEdge);
}

float2 ResolveFaceHairSampleDirection()
{
    const float3 lightDirection = normalize(
        dot(gDirectionalLight.xyz, gDirectionalLight.xyz) > 1.0e-6f
            ? -gDirectionalLight.xyz
            : float3(-0.4f, 1.0f, 0.6f));
    const float4 projected = mul(gViewProj, float4(lightDirection, 0.0f));
    float2 direction = float2(projected.x, -projected.y);
    const float directionLength = length(direction);
    return directionLength > 1.0e-5f
        ? direction / directionLength
        : normalize(float2(-0.65f, -0.75f));
}

float ResolveFaceHairShadow(
    int2 centerPixel,
    float centerDepth,
    uint2 centerFeature)
{
    const bool enabled =
        (gFeatureFlags.x & (1u << 3u)) != 0u &&
        (centerFeature.x & HIKARI_STYLIZED_FEATURE_FACE_HAIR_SHADOW) != 0u &&
        (centerFeature.x & HIKARI_STYLIZED_ROLE_FACE) != 0u;
    if (!enabled)
        return 0.0f;

    const float strength =
        HikariDecodeStylizedFaceHairStrength(centerFeature.x) *
        gFaceHairParams.x;
    const float offsetPixels = max(
        0.5f,
        HikariDecodeStylizedFaceHairOffset(centerFeature.x));
    const float2 direction = ResolveFaceHairSampleDirection();
    float coverage = 0.0f;
    [unroll]
    for (uint tap = 1u; tap <= 4u; ++tap)
    {
        const float distancePixels = offsetPixels * (float(tap) / 4.0f);
        const int2 samplePixel = ClampSurfacePixel(
            centerPixel + int2(round(direction * distancePixels)));
        const uint2 sampleFeature = LoadSurfaceFeature(samplePixel);
        if ((sampleFeature.x & HIKARI_STYLIZED_ROLE_HAIR) == 0u ||
            !HikariStylizedGroupsMatch(centerFeature.y, sampleFeature.y))
            continue;
        const float sampleDepth = gSceneDepth.Load(int3(samplePixel, 0));
        const float depthVisible =
            sampleDepth <= centerDepth + gFaceHairParams.y ? 1.0f : 0.0f;
        coverage = max(coverage, depthVisible * (1.0f - float(tap - 1u) * 0.18f));
    }
    return saturate(coverage * strength);
}

float4 PSMain(FullscreenVertex input) : SV_TARGET
{
    const float4 scene = gSceneColor.SampleLevel(
        gPointClamp, input.uv, 0.0f);
    const int2 centerPixel = ClampSurfacePixel(int2(input.position.xy));
    const uint2 centerFeature = LoadSurfaceFeature(centerPixel);
    const float centerDepth = gSceneDepth.SampleLevel(
        gPointClamp, input.uv, 0.0f);
    const float3 centerNormal = HikariDecodeOctahedralNormal(
        gPackedSurface.SampleLevel(gPointClamp, input.uv, 0.0f).rg);
    const float4 outlineStyle = ResolveOutlineStyle(centerPixel);
    const float2 texel = gScreenParams.zw * outlineStyle.w;
    float edge = 0.0f;
    if (IsOutlineSurface(centerFeature.x))
    {
        edge = max(edge, ResolveEdge(input.uv, float2(texel.x, 0.0f),
                                     centerDepth, centerNormal));
        edge = max(edge, ResolveEdge(input.uv, float2(-texel.x, 0.0f),
                                     centerDepth, centerNormal));
        edge = max(edge, ResolveEdge(input.uv, float2(0.0f, texel.y),
                                     centerDepth, centerNormal));
        edge = max(edge, ResolveEdge(input.uv, float2(0.0f, -texel.y),
                                     centerDepth, centerNormal));
    }
    float3 color = scene.rgb *
        (1.0f - ResolveFaceHairShadow(
            centerPixel, centerDepth, centerFeature));
    const float blend = smoothstep(0.85f, 1.15f, edge) * gEdgeParams.w;
    return float4(lerp(color, outlineStyle.rgb, blend), scene.a);
}
