#ifndef HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED
#define HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED

void ResolveSurfaceLightmap(
    uint surfaceGpuSceneIndex,
    float2 uv1,
    out float3 irradiance,
    out float weight)
{
    irradiance = 0.0f.xxx;
    weight = 0.0f;
#if HIKARI_SURFACE_GPU_SCENE_CONSUME
    if (!HikariCanConsumeSurfaceGpuSceneForPixel() ||
        gSurfaceLightmapEnabled < 0.5f)
    {
        return;
    }
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    const uint descriptorIndex = instance.lightmapTextureDescriptorIndex;
    if (descriptorIndex == 0xffffffffu ||
        descriptorIndex >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT)
    {
        return;
    }
    const float2 lightmapUv =
        uv1 * instance.lightmapUvScaleBias.xy +
        instance.lightmapUvScaleBias.zw;
    irradiance = gMaterialTexturePool[
        NonUniformResourceIndex(descriptorIndex)].Sample(
            gLinearClamp,
            saturate(lightmapUv)).rgb;
    weight = 1.0f;
#endif
}

HikariDirectPbrLobes AccumulateLocalLightPbrLobes(
    HikariPbrShadingContext pbrContext,
    float3 worldPosWS,
    float2 pixelPosition, float3 geometricNormalWS, uint receiveShadow)
{
    HikariDirectPbrLobes sum = (HikariDirectPbrLobes)0;
    // Cluster のリストは隣接画素と異なるため、微分はループの外で一度だけ取る。
    const HikariPbrPixelFootprint footprint = HikariBuildPbrPixelFootprint(pbrContext, worldPosWS);
    const uint2 cluster = HikariResolveLocalLightCluster(
        pixelPosition,
        HikariCameraViewDepth(worldPosWS));
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const uint i = gHikariLocalLightClusterIndices[cluster.x + slot];
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[i],
                worldPosWS,
                lightSample))
        {
            continue;
        }
        const HikariDirectPbrLobes lobes = HikariEvaluateDirectPbrLobes(
            HikariFilterLocalPbrContext(pbrContext, footprint, lightSample.directionToLight,
                lightSample.distanceToLight, lightSample.sourceHalfAngleSineSquared),
            lightSample.directionToLight, lightSample.radiance);
        const float visibility = SampleLocalLightVisibility(
            gHikariLocalLights[i], worldPosWS, geometricNormalWS, receiveShadow);
        sum.diffuse += lobes.diffuse * visibility;
        sum.specular += lobes.specular * visibility;
    }
    return sum;
}

float3 AccumulateLocalLightsPbr(
    HikariPbrShadingContext context, float3 worldPosWS, float2 pixelPosition,
    float3 geometricNormalWS, uint receiveShadow)
{
    const HikariDirectPbrLobes lobes = AccumulateLocalLightPbrLobes(
        context, worldPosWS, pixelPosition, geometricNormalWS, receiveShadow);
    return lobes.diffuse + lobes.specular;
}

#include "../Material/HIKARI_MaterialPbrInputs.hlsli"

float HikariForwardFogFactor(float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return 0.0f;
    }
    const float dist = length(gCameraPos.xyz - worldPosWS) * 0.8f;
    const float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(
        fogFactor * max(0.0f, gFogColorDensity.a) * dist);
    return fogFactor;
}

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }
    return lerp(color, gFogColorDensity.rgb, HikariForwardFogFactor(worldPosWS));
}

#endif
