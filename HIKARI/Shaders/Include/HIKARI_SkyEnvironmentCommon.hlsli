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
    float4 gSkyScales;
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
    HikariCloudSolarVisibilityData gCloudSolarVisibility;
    HikariIrradianceVolumeData gDynamicGiVolumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    // 流体の烟が場面へ落とす影（雲の影と同じ並び、面の太陽に掛ける）。
    HikariCloudSolarVisibilityData gFluidSolarVisibility;
};

// 空の画素値を輝度 [nits] へ写す倍率（大気は 1）、反射と天空光の倍率。
#define gSkyRadianceScale gSkyScales.x
#define gSkyReflectionIntensity gSkyScales.y
#define gSkyLightIntensity gSkyScales.z
#define gSkyMode gSkyParams.x
#define gSkyHasCubemap gSkyParams.y
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

// 大気・全景・IBL を持たない立方体の空は、実行時に捕獲した立方体（gIbl*）だけを光源にする。
// 捕獲が揃うまでの数フレームは空の光を 0 にする（古い空や別の近似を混ぜない）。
bool HikariSkyUsesWorldSpaceCapture()
{
    return (uint)(gSkyMode + 0.5f) == 4u;
}

float3 HikariSkyEnvironmentDirection(float3 direction)
{
    return normalize(HikariSkyUsesWorldSpaceCapture() ? direction : HikariRotateSkyYaw(direction, gSkyYaw));
}

// 空からの拡散光（反照率 1 の面の出射輝度）。天空光の倍率を含む。
float3 HikariEvaluateSkyDiffuseIrradiance(float3 normal)
{
    if (gIblHasIrradiance <= 0.5f)
        return 0.0f.xxx;
    const float3 irradiance = gIblIrradianceTex.Sample(gLinearWrap, HikariSkyEnvironmentDirection(normal)).rgb;
    return HikariApplySkyRadianceScale(irradiance, gSkyTint, gSkyRadianceScale) * max(gSkyLightIntensity, 0.0f);
}

// 空そのものの輝度（反射の代わりや水平線の色）。立方体と全景は元の画像、大気は捕獲した立方体から引く。
float3 HikariSampleSkyEnvironment(float3 dir)
{
    const uint mode = (uint)(gSkyMode + 0.5f);
    const float3 direction = HikariSkyEnvironmentDirection(dir);
    float3 sky = 0.0f.xxx;
    if (mode == 2u && gSkyHasCubemap > 0.5f)
    {
        sky = gSkyCube.Sample(gLinearWrap, direction).rgb;
    }
    else if (mode == 3u && gSkyTextureDescriptorIndexPlusOne > 0.5f)
    {
        const float2 panoramaUv = HikariDirectionToEquirectangularUv(direction);
        float2 panoramaDerivativeX;
        float2 panoramaDerivativeY;
        HikariResolveEquirectangularGradients(direction, panoramaUv, panoramaDerivativeX, panoramaDerivativeY);
        const uint panoramaDescriptorIndex = (uint)(gSkyTextureDescriptorIndexPlusOne + 0.5f) - 1u;
        sky = HikariSampleMaterialTextureGrad(panoramaDescriptorIndex, gSkyPanoramaSampler, panoramaUv,
            panoramaDerivativeX, panoramaDerivativeY, 0.0f.xxxx).rgb;
    }
    else if (gIblHasPrefiltered > 0.5f)
    {
        sky = gIblPrefilteredTex.SampleLevel(gLinearWrap, direction, 0.0f).rgb;
    }
    return HikariApplySkyRadianceScale(sky, gSkyTint, gSkyRadianceScale);
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
float3 HikariSamplePrefilteredSkyRadiance(float3 r, float roughness)
{
    const float mip = roughness * (max(1.0f, gIblPrefilteredMipCount)-1.0f);
    return HikariApplySkyRadianceScale(
        gIblPrefilteredTex.SampleLevel(gLinearWrap, HikariSkyEnvironmentDirection(r), mip).rgb,
        gSkyTint, gSkyRadianceScale);
}

// skyOcclusion は空の反射の遮蔽、occludedRadiance は遮られた分に映す周囲の輝度。反射 probe には掛けない。
float3 HikariSampleEnvironmentReflection(float3 worldPos, float3 r, float roughness, float skyOcclusion = 1.0f,
    float3 occludedRadiance = 0.0f.xxx)
{
    float3 skyRadiance = 0.0f.xxx;
    if (gIblHasPrefiltered > 0.5f)
        skyRadiance = HikariSamplePrefilteredSkyRadiance(r, roughness);
    else
    {
        skyRadiance = HikariSampleSkyEnvironment(r);
    }
    skyRadiance = skyRadiance * max(0.0f, gSkyReflectionIntensity) * skyOcclusion +
        occludedRadiance * (1.0f - skyOcclusion);

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
