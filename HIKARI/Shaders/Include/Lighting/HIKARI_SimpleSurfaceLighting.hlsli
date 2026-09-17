#ifndef HIKARI_SIMPLE_SURFACE_LIGHTING_INCLUDED
#define HIKARI_SIMPLE_SURFACE_LIGHTING_INCLUDED
float3 HikariSimpleSurfaceNormalize(float3 value, float3 fallback) {
    const float lengthSquared = dot(value,value);
    return lengthSquared > 1.0e-8f ? value*rsqrt(lengthSquared) : fallback;
}
// Shared surface response for lit particles and world sprites.
float3 HikariShadeSimpleSurface(float3 baseColor, float metallic, float roughness, float3 specularColor, float specularFactor, float occlusion,
    float3 normal, float3 geometricNormal, float3 worldPosition, float2 pixelPosition, uint receiveShadow = 1u)
{
    const float3 view = HikariCameraViewDirection(worldPosition);
    const HikariPbrShadingContext pbr = HikariBuildPbrShadingContext(baseColor,metallic,
        roughness,specularColor,specularFactor,normal,view);
    float3 direct = HikariEvaluateDirectionalPbr(pbr,HikariSimpleSurfaceNormalize(-gDirectionalDir.xyz,float3(0,1,0)),
        gDirectionalColor.rgb,gDirectionalIntensity,gDirectionalDir.w) *
        SampleDirectionalVisibility(worldPosition,geometricNormal,receiveShadow);
    if (gCloudSolarVisibility.originDescriptor.w > 0.5f)
        direct *= HikariSampleCloudSolarVisibility(gMaterialTexturePool[NonUniformResourceIndex(
            uint(gCloudSolarVisibility.originDescriptor.w)-1u)],gCloudSolarVisibility,worldPosition);
    direct += AccumulateLocalLightsPbr(pbr,worldPosition,pixelPosition,geometricNormal,receiveShadow);
    float3 irradiance = gAmbientColor.rgb*gAmbientIntensity;
    if (gSkyAmbientFromSky > 0.0001f)
        irradiance = HikariBlendAmbientWithSky(irradiance,HikariEvaluateSkyDiffuseIrradiance(normal),gSkyAmbientFromSky);
    const float3 reflection = reflect(-view,normal);
    const float3 environment = (gIblHasPrefiltered > 0.5f ?
        HikariSamplePrefilteredSkyRadiance(reflection,pbr.roughness) : HikariEvaluateSkyApprox(reflection)) *
        max(gSkyReflectionIntensity,0.0f);
    // Sky/ambient は通常 Forward と同じ E/pi。ここで再び pi を除かない。
    return direct + (irradiance*baseColor*pbr.diffuseEnergyWeight +
        environment*pbr.environmentSpecularResponse)*occlusion;
}

#endif
