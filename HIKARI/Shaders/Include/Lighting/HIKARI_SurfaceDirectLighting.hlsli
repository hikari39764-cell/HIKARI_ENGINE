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
    // 隣接画素の微分を取り終えてから早期終了する。
    if (pbrContext.ndotView <= 0.0f)
        return sum;
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
        // BRDF が 0 を返す半球では、配光・高光の濾波・影の読み出しも不要。
        // 面光源の拡散と鏡面は別の代表点なので、それぞれの向きを調べる。
        const bool diffuseFacing = dot(pbrContext.normal, lightSample.directionToLight) > 0.0f;
        const bool separateSpecular = any(lightSample.specularDirectionToLight != lightSample.directionToLight);
        const bool specularFacing = separateSpecular
            ? dot(pbrContext.normal, lightSample.specularDirectionToLight) > 0.0f : diffuseFacing;
        if (!diffuseFacing && !specularFacing)
            continue;
        HikariApplyLocalLightTextures(gHikariLocalLights[i], lightSample);
        HikariDirectPbrLobes lobes = (HikariDirectPbrLobes)0;
        if (diffuseFacing)
        {
            lobes = HikariEvaluateDirectPbrLobes(
                HikariFilterLocalPbrContext(pbrContext, footprint, lightSample.directionToLight,
                    lightSample.distanceToLight, lightSample.sourceHalfAngleSineSquared),
                lightSample.directionToLight, lightSample.radiance);
        }
        if (separateSpecular)
        {
            lobes.specular = 0.0f.xxx;
            if (specularFacing)
                lobes.specular = HikariEvaluateDirectPbrLobes(
                    HikariFilterLocalPbrContext(pbrContext, footprint, lightSample.specularDirectionToLight,
                        lightSample.distanceToLight, lightSample.sourceHalfAngleSineSquared),
                    lightSample.specularDirectionToLight, lightSample.specularRadiance).specular;
        }
        if (all(lobes.diffuse == 0.0f) && all(lobes.specular == 0.0f))
            continue;
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
