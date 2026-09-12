#ifndef HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI
#define HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI

#include "Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Sky/HIKARI_SkyRadiance.hlsli"

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

float3 HikariEvaluateAtmosphereSunTerm(float3 dir)
{
    // gAtmosphereSunColorRadiance.a is source radiance. A narrow reflected
    // disk is observed through the renderer-provided solar transmittance.
    return HikariAtmosphereEvaluateSunDisk(
        dir,
        max(0.0f.xxx, gAtmosphereSunColorRadiance.rgb) *
            max(0.0f, gAtmosphereSunRadiance));
}

float3 HikariSampleSkyEnvironment(float3 dir)
{
    float3 sky = HikariEvaluateSkyApprox(dir);
    uint mode = (uint)(gSkyMode + 0.5f);
    if (mode == 4u && gAtmosphereSunEnabled > 0.5f)
    {
        // Keep the reflected sun disk on the same authoritative direction,
        // color, radiance, and angular radius as SkyPS. Legacy modes retain
        // their existing cubemap/analytic fallback behavior below.
        sky = HikariEvaluateAtmosphereSky(dir) +
            HikariEvaluateAtmosphereSunTerm(dir) *
            max(gSkyTint, 0.0f.xxx) *
            max(0.0f, gSkyExposure);
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

#include "Forward/HIKARI_LocalLightingProviderCommon.hlsli"
#include "Forward/HIKARI_AmbientIblCommon.hlsli"

#endif
