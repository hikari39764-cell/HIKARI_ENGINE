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

float3 HikariSampleSkyEnvironment(float3 dir)
{
    float3 sky = HikariEvaluateSkyApprox(dir);
    uint mode = (uint)(gSkyMode + 0.5f);
    if (mode == 2u && gSkyHasCubemap > 0.5f)
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
