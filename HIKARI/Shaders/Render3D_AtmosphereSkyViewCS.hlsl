#ifndef HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED
#define HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED

cbuffer AtmosphereLutCB : register(b0)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams; // width, aerial perspective, observer sun T, albedo
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance; // source color, source radiance
    float4 gAtmosphereGroundColor;
};

Texture2D<float4> gAtmosphereTransmittance : register(t0);
Texture2D<float4> gAtmosphereMultiScattering : register(t1);
RWTexture2D<float4> gAtmosphereSkyView : register(u0);
SamplerState gLinearClamp : register(s0);

#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0u;
    uint height = 0u;
    gAtmosphereSkyView.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) /
        float2(max(1u, width), max(1u, height));
    const float elevation = uv.y * 2.0f - 1.0f;
    const float azimuth = uv.x * 6.28318530718f;
    const float3 viewDirection = HikariAtmosphereSafeNormalize(float3(
        cos(azimuth) * sqrt(max(0.0f, 1.0f - elevation * elevation)),
        elevation,
        sin(azimuth) * sqrt(max(0.0f, 1.0f - elevation * elevation))));
    // The LUT is authored for a stable ground observer. Per-frame camera
    // altitude is not part of the atmosphere parameter contract, so the
    // renderer keeps this fixed just above the planet surface.
    const float3 observer = float3(
        0.0f,
        HikariAtmospherePlanetRadius() + 1.0f,
        0.0f);
    const float aerialPerspectiveStrength = max(
        0.0f,
        HikariAtmosphereFiniteOr(gAtmosphereOzoneParams.y, 1.0f));
    const float3 groundColor = gAtmosphereGroundColor.rgb;
    float3 radiance = HikariAtmosphereEvaluateSkyRadiance(
        observer,
        viewDirection,
        groundColor,
        gAtmosphereOzoneParams.w,
        aerialPerspectiveStrength);
    const float3 sunDirection = HikariAtmosphereSafeNormalize(
        gAtmosphereSunDirection.xyz);
    const float sunCosine = dot(float3(0.0f, 1.0f, 0.0f), sunDirection);
    const float radiusUv = saturate(1.0f /
        HikariAtmosphereTopHeight());
    const float2 multiUv = float2(
        saturate(sunCosine * 0.5f + 0.5f),
        radiusUv);
    const float3 multi = gAtmosphereMultiScattering.SampleLevel(
        gLinearClamp, multiUv, 0.0f).rgb;
    radiance += max(0.0f.xxx, gAtmosphereSunColorRadiance.rgb) *
        max(0.0f, HikariAtmosphereFiniteOr(
            gAtmosphereSunColorRadiance.a,
            0.0f)) * multi * aerialPerspectiveStrength;
    gAtmosphereSkyView[dispatchThreadId.xy] =
        float4(
            all(isfinite(radiance)) ? max(0.0f.xxx, radiance) : 0.0f.xxx,
            1.0f);
}

#endif
