#ifndef HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED
#define HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED

#define HIKARI_ATMOSPHERE_DECLARE_LUT_CB


Texture2D<float4> gAtmosphereTransmittance : register(t0);
Texture2D<float4> gAtmosphereMultiScattering : register(t1);
RWTexture2D<float4> gAtmosphereSkyView : register(u0);
SamplerState gLinearClamp : register(s0);

#define HIKARI_ATMOSPHERE_USE_TRANSMITTANCE_LUT
#define HIKARI_ATMOSPHERE_USE_MULTISCATTERING_LUT
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
    gAtmosphereSkyView[dispatchThreadId.xy] =
        float4(
            all(isfinite(radiance)) ? max(0.0f.xxx, radiance) : 0.0f.xxx,
            1.0f);
}

#endif
