#ifndef HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI
#define HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI

float3 HikariRotateSkyYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);

    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    );
}

float3 HikariEvaluateSkyApprox(float3 dir)
{
    dir = normalize(
        HikariRotateSkyYaw(dir, gSkyYaw));
    float y = saturate(
        dir.y * 0.5f + 0.5f);
    float3 upper = lerp(
        gSkyHorizonColor,
        gSkyZenithColor,
        y);
    float3 lower = lerp(
        gSkyGroundColor,
        gSkyHorizonColor,
        y);
    float3 color =
        dir.y >= 0.0f ? upper : lower;
    float horizon = pow(
        saturate(1.0f - abs(dir.y)),
        max(0.01f, gSkyHorizonPower)
    );

    color = lerp(
        color,
        gSkyHorizonColor,
        horizon * 0.25f);
    color *= max(0.0f, gSkyExposure);
    return color;
}

float3 HikariAtmosphereSafeNormalize(float3 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

float3 HikariEvaluateAtmosphereSky(float3 dir)
{
    if (gAtmosphereSunEnabled <= 0.5f)
    {
        return HikariEvaluateSkyApprox(dir);
    }

    dir = HikariAtmosphereSafeNormalize(dir);
    const float3 sunDirection = HikariAtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float sunCosine = dot(dir, sunDirection);
    const float rayleighPhase =
        0.05968310366f * (1.0f + sunCosine * sunCosine);
    const float mieG = clamp(gAtmosphereMie.a, -0.95f, 0.95f);
    const float miePhase = 0.07957747155f *
        (1.0f - mieG * mieG) /
        pow(max(1.0e-3f, 1.0f + mieG * mieG -
            2.0f * mieG * sunCosine), 1.5f);

    const float topHeight = max(
        1.0f,
        gAtmosphereTopRadius - gAtmospherePlanetRadius);
    const float altitude = clamp(
        max(0.0f, dir.y) * topHeight,
        0.0f,
        topHeight);
    const float rayleighDensity = exp(-altitude / max(
        1.0f,
        gAtmosphereRayleighScaleHeight));
    const float mieDensity = exp(-altitude / max(
        1.0f,
        gAtmosphereMieScaleHeight));
    const float ozoneBand = exp(-abs(
        altitude - gAtmosphereOzone.a) / max(1.0f, gAtmosphereOzoneParams.x));
    const float3 extinction =
        gAtmosphereRayleigh.rgb * rayleighDensity +
        gAtmosphereMie.rgb * mieDensity +
        gAtmosphereOzone.rgb * ozoneBand;
    const float pathLength = lerp(0.01f, 1.0f, saturate(1.0f - dir.y)) * topHeight;
    const float3 transmittance = exp(-max(0.0f.xxx, extinction * pathLength));
    const float3 integratedScattering =
        1.0f.xxx - exp(-max(0.0f.xxx, extinction * pathLength));
    const float aerialPerspectiveStrength =
        max(0.0f, gAtmosphereOzoneParams.y);
    const float3 scatteringShare =
        (gAtmosphereRayleigh.rgb * rayleighDensity * rayleighPhase +
         gAtmosphereMie.rgb * mieDensity * miePhase) /
        max(extinction, 1.0e-6f.xxx);
    const float3 singleScattering =
        gAtmosphereSunColorRadiance.rgb *
        gAtmosphereSunRadiance *
        integratedScattering * scatteringShare * transmittance *
        aerialPerspectiveStrength;
    const float horizon = pow(
        saturate(1.0f - abs(dir.y)),
        max(0.01f, gSkyHorizonPower));
    const float3 multiScattering =
        gAtmosphereSunColorRadiance.rgb *
        gAtmosphereSunRadiance *
        (1.0f.xxx - transmittance) * aerialPerspectiveStrength *
        (0.18f + 0.32f * horizon);
    const float3 groundBounce =
        gSkyGroundColor * max(0.0f, gAtmosphereOzoneParams.w) *
        (1.0f - saturate(dir.y)) * 0.15f;
    return max(
        0.0f.xxx,
        (singleScattering + multiScattering + groundBounce) *
            max(0.0f, gSkyExposure) * max(0.0f.xxx, gSkyTint));
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
    const float3 sunDirection = HikariAtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float angularRadius = max(1.0e-4f, gAtmosphereRayleigh.a);
    const float disk = smoothstep(
        1.0f - angularRadius,
        1.0f,
        dot(HikariAtmosphereSafeNormalize(dir), sunDirection));
    return disk * gAtmosphereSunColorRadiance.rgb *
        max(0.0f, gAtmosphereSunRadiance);
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
    return sky;
}

#include "Forward/HIKARI_LocalLightingProviderCommon.hlsli"
#include "Forward/HIKARI_AmbientIblCommon.hlsli"

#endif
