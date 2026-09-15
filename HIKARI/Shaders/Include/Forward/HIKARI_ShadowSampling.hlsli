#ifndef HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED
#define HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED

#include "Include/Lighting/HIKARI_ShadowCascadeContract.hlsli"
#include "Include/Lighting/HIKARI_ShadowPcf.hlsli"

float2 HikariClampShadowAtlasUv(float2 uv, uint cascadeIndex)
{
    return HikariClampShadowAtlasTileUv(uv, cascadeIndex, gShadowCascadeParams.zw,
        float2(gShadowTexelSizeX, gShadowTexelSizeY));
}

float HikariSampleShadowPcf(
    float2 uv,
    float currentDepth,
    uint cascadeIndex)
{
    return HikariShadowPcf5x5(gShadowMap, gShadowSampler, uv, currentDepth,
        float2(gShadowTexelSizeX, gShadowTexelSizeY),
        HikariClampShadowAtlasUv(0.0f.xx, cascadeIndex),
        HikariClampShadowAtlasUv(1.0f.xx, cascadeIndex),
        gShadowPcfEnabled != 0 ? gShadowPcfRadius : 0.0f);
}

float HikariShadowReceiverFade(float2 uv)
{
    if (gShadowEdgeFade <= 0.00001f)
    {
        return 1.0f;
    }

    const float edgeDistance =
        min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    return saturate(edgeDistance / gShadowEdgeFade);
}

float2 HikariShadowAtlasUv(float2 cascadeUv, uint cascadeIndex)
{
    return
        cascadeUv * gShadowCascadeParams.zw +
        HikariShadowAtlasTileOrigin(cascadeIndex);
}

float3 HikariApplyBoundedShadowNormalBias(
    float3 worldPosWS,
    float3 geometricNormalWS,
    uint cascadeIndex)
{
    const float3 requestedOffset =
        geometricNormalWS * max(gShadowNormalBias, 0.0f);
    const float3 projectedOffset =
        mul(
            gShadowLightViewProj[cascadeIndex],
            float4(requestedOffset, 0.0f)).xyz;

    // Authoring remains in world units, but the effective offset is bounded in
    // the selected cascade. This prevents a large acne fix from detaching a
    // near-cascade shadow by many texels.
    const float maximumProjectedOffset =
        max(gShadowTexelSizeX, gShadowTexelSizeY) * 8.0f;
    const float projectedXY =
        max(abs(projectedOffset.x), abs(projectedOffset.y));
    const float xyScale =
        projectedXY > maximumProjectedOffset
            ? maximumProjectedOffset / projectedXY
            : 1.0f;
    const float maximumDepthOffset =
        max(gShadowDepthBias * 4.0f, 0.002f) * gShadowCascadeDepthBiasScales[cascadeIndex];
    const float projectedZ = abs(projectedOffset.z);
    const float depthScale =
        projectedZ > maximumDepthOffset
            ? maximumDepthOffset / projectedZ
            : 1.0f;
    return worldPosWS + requestedOffset * min(xyScale, depthScale);
}

float HikariSampleDirectionalShadowCascade(
    float3 worldPosWS,
    float3 geometricNormalWS,
    uint cascadeIndex,
    out bool cascadeValid)
{
    const float3 biasedWorldPos =
        HikariApplyBoundedShadowNormalBias(
            worldPosWS,
            geometricNormalWS,
            cascadeIndex);
    const float4 lightClip =
        mul(gShadowLightViewProj[cascadeIndex], float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        cascadeValid = false;
        return 1.0f;
    }

    const float3 proj = lightClip.xyz / lightClip.w;
    const float2 cascadeUv =
        float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (cascadeUv.x < 0.0f || cascadeUv.x > 1.0f ||
        cascadeUv.y < 0.0f || cascadeUv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        cascadeValid = false;
        return 1.0f;
    }

    cascadeValid = true;
    const float2 atlasUv =
        HikariShadowAtlasUv(cascadeUv, cascadeIndex);
    const float currentDepth = proj.z - gShadowDepthBias * gShadowCascadeDepthBiasScales[cascadeIndex];
    const float sampledVisibility =
        HikariSampleShadowPcf(
            atlasUv,
            currentDepth,
            cascadeIndex);
    return lerp(
        1.0f,
        sampledVisibility,
        HikariShadowReceiverFade(cascadeUv));
}

float SampleDirectionalVisibility(float3 worldPosWS, float3 geometricNormalWS, uint receiveShadow)
{
    if (gShadowEnabled == 0 || receiveShadow == 0)
    {
        return 1.0f;
    }

    // CPU cascade fitting uses axial camera depth. Clip W preserves that same
    // contract even for receivers near the edges of a wide view.
    const float viewDistance = HikariCameraViewDepth(worldPosWS);
    const uint cascadeCount =
        clamp((uint)round(gShadowCascadeParams.x), 1u, 4u);
    const uint cascadeIndex = HikariSelectShadowCascade(
        viewDistance, gShadowCascadeSplits, cascadeCount);
    bool cascadeValid = false;
    float visibility =
        HikariSampleDirectionalShadowCascade(
            worldPosWS,
            geometricNormalWS,
            cascadeIndex,
            cascadeValid);
    if (!cascadeValid && cascadeIndex + 1u < cascadeCount)
    {
        visibility =
            HikariSampleDirectionalShadowCascade(
                worldPosWS,
                geometricNormalWS,
                cascadeIndex + 1u,
                cascadeValid);
    }

    const float blend = HikariShadowCascadeBlendWeight(
        viewDistance, gShadowCascadeSplits, cascadeIndex, gShadowCascadeParams.y);
    if (cascadeIndex + 1u < cascadeCount && blend > 0.0f)
    {
        bool nextCascadeValid = false;
        const float nextVisibility =
            HikariSampleDirectionalShadowCascade(
                worldPosWS,
                geometricNormalWS,
                cascadeIndex + 1u,
                nextCascadeValid);
        if (nextCascadeValid)
        {
            visibility = lerp(visibility, nextVisibility, blend);
        }
    }
    return visibility;
}

float SampleLocalLightVisibility(HikariLocalLightGpuData light, float3 worldPosWS,
    float3 geometricNormalWS, uint receiveShadow)
{
    if (receiveShadow == 0u || (light.kind >> 16u) == 0u || gLocalShadow.faceCount == 0u ||
        gLocalShadow.textureDescriptorIndex >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT) return 1.0f;
#if HIKARI_FORWARD_COST_MODE_STATIC == 3
    return 1.0f;
#elif HIKARI_FORWARD_COST_MODE_STATIC == 255
    if ((uint)(gForwardCostMode + 0.5f) == HIKARI_FORWARD_COST_NO_SHADOW) return 1.0f;
#endif
    return HikariSampleLocalLightShadow(
        gMaterialTexturePool[NonUniformResourceIndex(gLocalShadow.textureDescriptorIndex)], gShadowSampler,
        gLocalShadow, light, worldPosWS, geometricNormalWS);
}

float HikariStylizedShadowFactor(float visibility)
{
    // 陰影の演出強度は物理的な可視性とは別契約。PBR 直射光へ漏らさない。
    return lerp(1.0f, saturate(visibility), saturate(gShadowStrength));
}

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS, uint receiveShadow)
{
    return HikariStylizedShadowFactor(
        SampleDirectionalVisibility(worldPosWS, geometricNormalWS, receiveShadow));
}

#endif
