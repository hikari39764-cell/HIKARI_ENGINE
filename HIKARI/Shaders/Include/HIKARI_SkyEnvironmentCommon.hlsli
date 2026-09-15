#ifndef HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI
#define HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI

// Forward と二次 ray が同じ環境照明を借用する。
// 既定の register space は既存 MeshGraphics root を維持する。
#ifndef HIKARI_ENVIRONMENT_REGISTER_SPACE
#define HIKARI_ENVIRONMENT_REGISTER_SPACE space0
#endif
#include "HIKARI_PbrCommon.hlsli"

struct HikariReflectionProbeData
{
    float4 positionRadius;
    float4 params;
    float4 intensity;
    float4 influenceBoxMin;
    float4 influenceBoxMax;
    float4 projectionBoxMin;
    float4 projectionBoxMax;
    float4 shapeParams;
};


#include "Lighting/HIKARI_IrradianceSh9.hlsli"
#include "Lighting/HIKARI_CloudSolarVisibility.hlsli"

cbuffer SkyEnvironmentCB : register(b5, HIKARI_ENVIRONMENT_REGISTER_SPACE)
{
    float4 gSkyZenithExposure;
    float4 gSkyHorizonReflection;
    float4 gSkyGroundAmbient;
    float4 gSkyParams;
    float4 gSkyTintLighting;
    float4 gIblParams;
    HikariReflectionProbeData gReflectionProbes[2];
    float4 gAmbientSamplingParams;
    HikariIrradianceVolumeData gIrradianceVolumes[2];
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams;
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
    float4 gGradientSkyIrradiance[3];
    HikariCloudSolarVisibilityData gCloudSolarVisibility;
};

#define gSkyZenithColor gSkyZenithExposure.rgb
#define gSkyExposure gSkyZenithExposure.a
#define gSkyHorizonColor gSkyHorizonReflection.rgb
#define gSkyReflectionIntensity gSkyHorizonReflection.a
#define gSkyGroundColor gSkyGroundAmbient.rgb
#define gSkyAmbientFromSky gSkyGroundAmbient.a
#define gSkyMode gSkyParams.x
#define gSkyHasCubemap gSkyParams.y
#define gSkyHorizonPower gSkyParams.z
#define gSkyYaw gSkyParams.w
#define gSkyTint gSkyTintLighting.rgb
#define gSkyTextureDescriptorIndexPlusOne gSkyTintLighting.w
#define gAtmospherePlanetRadius gAtmospherePlanetParams.x
#define gAtmosphereTopRadius gAtmospherePlanetParams.y
#define gAtmosphereRayleighScaleHeight gAtmospherePlanetParams.z
#define gAtmosphereMieScaleHeight gAtmospherePlanetParams.w
#define gAtmosphereSunEnabled gAtmosphereSunDirection.w
#define gAtmosphereLutsReady (gAtmosphereSunDirection.w > 1.5f)
#define gAtmosphereSunRadiance gAtmosphereSunColorRadiance.a
#define gIblHasIrradiance gIblParams.x
#define gIblHasPrefiltered gIblParams.y
#define gIblHasBrdfLut gIblParams.z
#define gIblPrefilteredMipCount gIblParams.w
#define gSsaoEnabled gAmbientSamplingParams.x
#define gSsaoDiffuseStrength gAmbientSamplingParams.y
#define gSsaoSpecularStrength gAmbientSamplingParams.z
#define gSurfaceLightmapEnabled gAmbientSamplingParams.w

TextureCube gSkyCube : register(t6, HIKARI_ENVIRONMENT_REGISTER_SPACE);
TextureCube gIblIrradianceTex : register(t9, HIKARI_ENVIRONMENT_REGISTER_SPACE);
TextureCube gIblPrefilteredTex : register(t10, HIKARI_ENVIRONMENT_REGISTER_SPACE);
Texture2D gIblBrdfLutTex : register(t11, HIKARI_ENVIRONMENT_REGISTER_SPACE);
TextureCube gReflectionProbePrefilteredTex0 : register(t12, HIKARI_ENVIRONMENT_REGISTER_SPACE);
TextureCube gReflectionProbePrefilteredTex1 : register(t14, HIKARI_ENVIRONMENT_REGISTER_SPACE);
// Light probe volume: SH9 係数を係数ごとに 1 枚の Texture3D (RGB=係数) に
// 焼き、hardware trilinear で係数を補間する。9 枚は root param LightProbeSh の
// descriptor table として連続確保される。
Texture3D<float4> gLightProbeShVolume0[9] : register(t0, space2);
Texture3D<float4> gLightProbeShVolume1[9] : register(t0, space4);

#include "Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Sky/HIKARI_SkyRadiance.hlsli"

void HikariApplyEnvironmentBrdf(inout HikariPbrShadingContext context)
{
    if (gIblHasBrdfLut > 0.5f || gReflectionProbes[0].params.z > 0.5f || gReflectionProbes[1].params.z > 0.5f)
    {
        // View/材質ごとに一度だけ解決し、直接光と環境光へ同じエネルギー応答を渡す。
        HikariSetPbrEnvironmentBrdf(context, gIblBrdfLutTex.SampleLevel(
            gLinearClamp, saturate(float2(context.ndotView, context.roughness)), 0.0f).rg);
    }
}

float3 HikariEvaluateSkyApprox(float3 dir)
{
    dir = normalize(
        HikariRotateSkyYaw(dir, gSkyYaw));
    return HikariApplySkyRadianceScale(HikariEvaluateGradientSky(dir,
        gSkyZenithColor, gSkyHorizonColor, gSkyGroundColor, gSkyHorizonPower), gSkyTint, gSkyExposure);
}

float3 HikariEvaluateAtmosphereSky(float3 dir)
{
    if (gAtmosphereSunEnabled <= 0.5f)
    {
        return HikariEvaluateSkyApprox(dir);
    }

    const float3 observer = float3(
        0.0f,
        HikariAtmospherePlanetRadius() + 1.0f,
        0.0f);
    // Forward/Water sampling is per-surface-pixel, so reuse the same
    // spherical model at a deliberately low bounded quality while the
    // SkyView LUT uses the full 24/12-step path.  Four view samples with two
    // solar samples preserve horizon extinction without turning every
    // material pixel into a second LUT bake.
    const float3 radiance = HikariAtmosphereEvaluateSkyRadianceWithSteps(
        observer,
        dir,
        gSkyGroundColor,
        gAtmosphereOzoneParams.w,
        gAtmosphereOzoneParams.y,
        4u,
        2u);
    return radiance * max(0.0f, gSkyExposure) *
        max(0.0f.xxx, gSkyTint);
}

float3 HikariEvaluateAtmosphereDiffuse(float3 normal)
{
    const float3 n = HikariAtmosphereSafeNormalize(normal);
    // A cosine-weighted representative direction keeps the diffuse fallback
    // tied to the same atmosphere as the visible sky without integrating the
    // narrow solar disk a second time.
    const float3 representativeDirection = HikariAtmosphereSafeNormalize(
        lerp(n, float3(0.0f, 1.0f, 0.0f), 0.45f));
    return HikariEvaluateAtmosphereSky(representativeDirection);
}

float3 HikariEvaluateSkyDiffuseIrradiance(float3 normal)
{
    if (gIblHasIrradiance > 0.5f)
    {
        const float3 direction=(uint)(gSkyMode+0.5f)==4u ? normal : HikariRotateSkyYaw(normal,gSkyYaw);
        const float3 irradiance = gIblIrradianceTex.Sample(gLinearWrap,
            normalize(direction)).rgb;
        return HikariApplySkyRadianceScale(irradiance, gSkyTint, gSkyExposure);
    }
    if ((uint)(gSkyMode + 0.5f) == 4u && gAtmosphereSunEnabled > 0.5f)
        return HikariEvaluateAtmosphereDiffuse(normal);
    return HikariApplySkyRadianceScale(
        HikariEvaluateGradientSkyIrradiance(normal, gGradientSkyIrradiance), gSkyTint, gSkyExposure);
}

float3 HikariSampleSkyEnvironment(float3 dir)
{
    float3 sky = HikariEvaluateSkyApprox(dir);
    uint mode = (uint)(gSkyMode + 0.5f);
    if (mode == 4u && gAtmosphereSunEnabled > 0.5f)
    {
        // 太陽の specular は直接光が所有する。raw/filtered fallback の両方で重複させない。
        sky = HikariEvaluateAtmosphereSky(dir);
    }
    else if (mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(
            HikariRotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(
            gLinearWrap,
            cubeDir).rgb;
        sky *= max(gSkyTint, 0.0f.xxx);
        sky *= max(0.0f, gSkyExposure);
    }
    else if (mode == 3u && gSkyTextureDescriptorIndexPlusOne > 0.5f)
    {
        const float3 panoramaDirection = normalize(
            HikariRotateSkyYaw(dir, gSkyYaw));
        const float2 panoramaUv =
            HikariDirectionToEquirectangularUv(panoramaDirection);
        float2 panoramaDerivativeX;
        float2 panoramaDerivativeY;
        HikariResolveEquirectangularGradients(
            panoramaDirection,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY);
        const uint panoramaDescriptorIndex =
            (uint)(gSkyTextureDescriptorIndexPlusOne + 0.5f) - 1u;
        sky = HikariSampleMaterialTextureGrad(
            panoramaDescriptorIndex,
            gSkyPanoramaSampler,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY,
            float4(sky, 1.0f)).rgb;
        sky *= max(gSkyTint, 0.0f.xxx);
        sky *= max(0.0f, gSkyExposure);
    }
    return sky;
}

#include "Lighting/HIKARI_LightingProviderSampling.hlsli"
float3 HikariSampleReflectionProbePrefiltered(
    uint providerSlot,
    float3 direction,
    float mip)
{
    return providerSlot == 0u
        ? gReflectionProbePrefilteredTex0.SampleLevel(
            gLinearWrap,
            direction,
            mip).rgb
        : gReflectionProbePrefilteredTex1.SampleLevel(
            gLinearWrap,
            direction,
            mip).rgb;
}

// provider 選択と視差補正は受光側 BRDF から独立させる。
// 一次面と二次面が同じ放射輝度 fallback を使う。
float3 HikariSampleEnvironmentReflection(float3 worldPos, float3 r, float roughness)
{
    float3 skyRadiance = 0.0f.xxx;
    if (gIblHasPrefiltered > 0.5f)
    {
        float mipCount =
            max(1.0f, gIblPrefilteredMipCount);
        float mip =
            roughness * (mipCount - 1.0f);
        float3 prefiltered =
            gIblPrefilteredTex.SampleLevel(
                gLinearWrap,
                normalize(
                    (uint)(gSkyMode+0.5f)==4u ? r : HikariRotateSkyYaw(r, gSkyYaw)),
                mip).rgb *
            max(gSkyTint, 0.0f.xxx) *
            max(0.0f, gSkyExposure);
        skyRadiance = prefiltered;
    }
    else
    {
        skyRadiance = HikariSampleSkyEnvironment(r);
    }
    skyRadiance *=
        max(0.0f, gSkyReflectionIntensity);

    float3 probeRadiance[2];
    float probeWeights[2];
    [unroll]
    for (uint slot = 0u; slot < 2u; ++slot)
    {
        HikariReflectionProbeData probe =
            gReflectionProbes[slot];
        if (probe.params.x < 0.5f ||
            probe.params.y < 0.5f)
        {
            probeWeights[slot] = 0.0f;
            probeRadiance[slot] = 0.0f.xxx;
            continue;
        }
        probeWeights[slot] =
            HikariEvaluateReflectionProbeInfluence(
                probe,
                worldPos);
        if (probeWeights[slot] <= 0.0001f)
        {
            probeWeights[slot] = 0.0f;
            probeRadiance[slot] = 0.0f.xxx;
            continue;
        }

        float3 probeDirection =
            HikariComputeReflectionProbeSampleDirection(
                probe,
                worldPos,
                r);
        float mip =
            roughness *
            (max(1.0f, probe.params.w) - 1.0f);
        float3 prefiltered =
            HikariSampleReflectionProbePrefiltered(
                (uint)slot,
                probeDirection,
                mip);
        probeRadiance[slot] = prefiltered;
        probeRadiance[slot] *=
            max(0.0f, probe.intensity.x);
    }

    float3 radiance = skyRadiance;
    float priority0 = gReflectionProbes[0].shapeParams.w;
    float priority1 = gReflectionProbes[1].shapeParams.w;
    if (priority0 > priority1)
    {
        float3 lower = lerp(
            skyRadiance,
            probeRadiance[1],
            probeWeights[1]);
        radiance = lerp(
            lower,
            probeRadiance[0],
            probeWeights[0]);
    }
    else if (priority1 > priority0)
    {
        float3 lower = lerp(
            skyRadiance,
            probeRadiance[0],
            probeWeights[0]);
        radiance = lerp(
            lower,
            probeRadiance[1],
            probeWeights[1]);
    }
    else
    {
        float totalWeight =
            probeWeights[0] + probeWeights[1];
        if (totalWeight > 0.0001f)
        {
            float3 localRadiance =
                (probeRadiance[0] * probeWeights[0] +
                    probeRadiance[1] * probeWeights[1]) /
                totalWeight;
            float localCoverage =
                1.0f -
                (1.0f - probeWeights[0]) *
                (1.0f - probeWeights[1]);
            radiance = lerp(
                skyRadiance,
                localRadiance,
                saturate(localCoverage));
        }
    }

    return radiance;
}

#include "Forward/HIKARI_AmbientIblCommon.hlsli"

#endif
