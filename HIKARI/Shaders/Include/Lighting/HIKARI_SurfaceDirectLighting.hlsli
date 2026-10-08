#ifndef HIKARI_SURFACE_DIRECT_LIGHTING_INCLUDED
#define HIKARI_SURFACE_DIRECT_LIGHTING_INCLUDED
#include "HIKARI_LocalLightTextures.hlsli"
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
    const float3 reflection = reflect(-pbrContext.view, pbrContext.normal);
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const uint i = gHikariLocalLightClusterIndices[cluster.x + slot];
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[i],
                worldPosWS,
                reflection,
                pbrContext.roughness,
                lightSample))
        {
            continue;
        }
        HikariApplyLocalLightTextures(gHikariLocalLights[i], lightSample);
        HikariDirectPbrLobes lobes = HikariEvaluateDirectPbrLobes(
            HikariFilterLocalPbrContext(pbrContext, footprint, lightSample.directionToLight,
                lightSample.distanceToLight, lightSample.sourceHalfAngleSineSquared),
            lightSample.directionToLight, lightSample.radiance);
        if (any(lightSample.specularDirectionToLight != lightSample.directionToLight))
        {
            // 面光源の鏡面は反射方向に最も近い発光体上の点から取る。
            lobes.specular = HikariEvaluateDirectPbrLobes(
                HikariFilterLocalPbrContext(pbrContext, footprint, lightSample.specularDirectionToLight,
                    lightSample.distanceToLight, lightSample.sourceHalfAngleSineSquared),
                lightSample.specularDirectionToLight, lightSample.specularRadiance).specular;
        }
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

#endif
