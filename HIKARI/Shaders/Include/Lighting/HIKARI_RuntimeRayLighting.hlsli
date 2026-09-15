#ifndef HIKARI_RUNTIME_RAY_LIGHTING_INCLUDED
#define HIKARI_RUNTIME_RAY_LIGHTING_INCLUDED
#include "HIKARI_RuntimeRayTracingQuery.hlsli"
#include "../HIKARI_PbrCommon.hlsli"
#include "HIKARI_LocalLightContract.hlsli"
#include "HIKARI_CloudSolarVisibility.hlsli"
#include "../Sky/HIKARI_SkyRadiance.hlsli"

struct HikariRayLightingData
{
    float4 sunDirectionIntensity;
    float4 sunColorRange;
    float4 ambient;
    float4 skyTintExposure;
    float4 zenith;
    float4 horizon;
    float4 ground;
    float4 skyParams;
    float4 time;
    uint4 scene;
    HikariCloudSolarVisibilityData clouds;
};
TextureCube<float4> gRuntimeRaySky : register(t0, space7);
StructuredBuffer<HikariLocalLightGpuData> gRuntimeRayLights : register(t1, space7);
StructuredBuffer<float2> gRuntimeRayLightCdf : register(t2, space7);

float3 HikariRayDirectLightResponse(bool includeSpecular, HikariPbrShadingContext pbr, float3 direction, float3 radiance)
{
    const HikariDirectPbrLobes lobes = HikariEvaluateDirectPbrLobes(pbr, direction, radiance);
    return lobes.diffuse + (includeSpecular ? lobes.specular : 0.0f.xxx);
}

float3 HikariRayEnvironmentRadiance(HikariRayLightingData lighting, float3 direction, SamplerState skySampler, float mipLevel = 0.0f)
{
    const float3 rotated = HikariRotateSkyYaw(direction,lighting.skyParams.y);
    const float3 raw = lighting.skyParams.x > 0.5f ? gRuntimeRaySky.SampleLevel(skySampler,rotated,mipLevel).rgb
        : HikariEvaluateGradientSky(rotated,lighting.zenith.rgb,lighting.horizon.rgb,lighting.ground.rgb,lighting.skyParams.z);
    return HikariBlendAmbientWithSky(lighting.ambient.rgb,
        HikariApplySkyRadianceScale(raw,lighting.skyTintExposure.rgb,lighting.skyTintExposure.w),lighting.skyParams.w);
}

bool HikariRayLightOccluded(HikariRayLightingData lighting, SamplerState materialSampler,
    HikariRaySurface surface, float3 direction, float distance)
{
    if (dot(surface.geometricNormal,direction) <= 0.0f) return true;
    if (lighting.scene.w != 0u) return false;
    RayDesc ray;
    ray.Origin = surface.position + surface.geometricNormal*0.002f;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = max(0.0011f,distance-0.003f);
    HikariRaySurface ignored;
    return HikariTraceRuntimeScene(ray,lighting.scene.x,lighting.scene.y,lighting.time,materialSampler,HIKARI_RAY_SHADOW,ignored);
}

float3 HikariRayHitRadiance(HikariRayLightingData lighting, HikariRaySurface hit, float3 view, float2 randomUv,
    float3 incidentDiffuse, bool includeSpecular, SamplerState materialSampler)
{
    const HikariMeshMaterialData material = HikariGetMeshMaterialData(hit.materialIndex);
    if ((material.materialFlags & HIKARI_MATERIAL_FLAG_UNLIT) != 0u) return hit.baseColor.rgb+hit.emissive;
    const HikariPbrShadingContext pbr = HikariBuildPbrShadingContextWithFootprint(hit.baseColor.rgb,
        hit.pbr.metallic,hit.pbr.roughness,hit.pbr.specularColor,hit.pbr.specularFactor,
        hit.normal,view,0.0f.xxx,0.0f.xxx);
    float3 result = hit.emissive;
    const float sunHalfAngle = sin(lighting.sunColorRange.w*0.5f);
    const float3 sunDirection = HikariSampleEmitterCone(lighting.sunDirectionIntensity.xyz,sunHalfAngle*sunHalfAngle,randomUv);
    float cloudVisibility=1.0f;
    if(lighting.clouds.originDescriptor.w>0.5f)
        cloudVisibility=HikariSampleCloudSolarVisibility(gMaterialTexturePool[NonUniformResourceIndex(uint(lighting.clouds.originDescriptor.w)-1u)],lighting.clouds,hit.position);
    if (lighting.sunDirectionIntensity.w > 0.0f && !HikariRayLightOccluded(lighting,materialSampler,hit,sunDirection,1e7f))
        result += HikariRayDirectLightResponse(includeSpecular,pbr,sunDirection,lighting.sunColorRange.rgb*lighting.sunDirectionIntensity.w /
            (1.0f-sunHalfAngle*sunHalfAngle)*cloudVisibility);
    if (lighting.scene.z != 0u)
    {
        uint lo = 0u, hi = lighting.scene.z-1u;
        while (lo < hi) { const uint middle=(lo+hi)/2u; if (randomUv.x < gRuntimeRayLightCdf[middle].x) hi=middle; else lo=middle+1u; }
        const HikariLocalLightGpuData light = gRuntimeRayLights[lo];
        HikariLocalLightSample sample;
        if (HikariResolveLocalLightSample(light,hit.position,sample))
        {
            const float3 direction = HikariSampleEmitterCone(sample.directionToLight,
                sample.sourceHalfAngleSineSquared,frac(randomUv.yx+float2(0.381966f,0.618034f)));
            const float cosine = dot(direction,sample.directionToLight);
            const float distance = sample.distanceToLight > light.sourceRadius
                ? sample.distanceToLight*cosine-sqrt(max(0.0f,light.sourceRadius*light.sourceRadius-
                    sample.distanceToLight*sample.distanceToLight*(1.0f-cosine*cosine))) : sample.distanceToLight;
            if ((light.kind >> 16u) == 0u || !HikariRayLightOccluded(lighting,materialSampler,hit,direction,distance))
                result += HikariRayDirectLightResponse(includeSpecular,pbr,direction,sample.radiance /
                    max(1e-8f,gRuntimeRayLightCdf[lo].y) / max(0.5f,1.0f-sample.sourceHalfAngleSineSquared));
        }
    }
    result += incidentDiffuse * pbr.baseColor * pbr.diffuseEnergyWeight;
    return all(isfinite(result)) ? max(result,0.0f.xxx) : 0.0f.xxx;
}

#endif
