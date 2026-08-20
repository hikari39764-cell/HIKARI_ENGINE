#ifndef HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED
#define HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED

cbuffer AtmosphereLutCB : register(b0)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams; // width, aerial perspective, observer sun T, albedo
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance; // source color, source radiance
};

Texture2D<float4> gAtmosphereTransmittance : register(t0);
RWTexture2D<float4> gAtmosphereMultiScattering : register(u0);
SamplerState gLinearClamp : register(s0);

#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0u;
    uint height = 0u;
    gAtmosphereMultiScattering.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) /
        float2(max(1u, width), max(1u, height));
    // The multi-scattering LUT shares the transmittance (mu, radius)
    // parameterization. Each texel integrates a small cosine-weighted set of
    // view paths, rather than turning one optical-depth scalar into a color.
    const float radius = lerp(
        HikariAtmospherePlanetRadius() + 0.5f,
        HikariAtmosphereTopRadius() - 0.5f,
        uv.y);
    const float sunMu = uv.x * 2.0f - 1.0f;
    const float3 origin = HikariAtmospherePositionFromRadius(radius);
    const float3 sunDirection = HikariAtmosphereDirectionFromMu(sunMu);
    const float2 transmittanceUv =
        HikariAtmosphereTransmittanceUvFromRadiusMu(radius, sunMu);
    const float3 transmittance = gAtmosphereTransmittance.SampleLevel(
        gLinearClamp,
        transmittanceUv,
        0.0f).rgb;
    float3 integratedScattering = 0.0f.xxx;
    [loop]
    for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
    {
        const float azimuth = ((float)sampleIndex + 0.5f) *
            (6.28318530718f / 8.0f);
        const float cosineElevation = lerp(
            0.12f,
            0.96f,
            ((float)(sampleIndex & 3u) + 0.5f) / 4.0f);
        const float sineElevation = sqrt(
            max(0.0f, 1.0f - cosineElevation * cosineElevation));
        const float3 viewDirection = HikariAtmosphereSafeNormalize(float3(
            sineElevation * cos(azimuth),
            cosineElevation,
            sineElevation * sin(azimuth)));
        integratedScattering += HikariAtmosphereIntegrateLowOrderScattering(
            origin,
            viewDirection,
            sunDirection,
            HIKARI_ATMOSPHERE_MAX_SUN_STEPS);
    }
    integratedScattering *= 1.0f / 8.0f;
    const float transmittanceLuminance = dot(
        saturate(transmittance),
        float3(0.2126f, 0.7152f, 0.0722f));
    const float multipleBounceWeight = lerp(
        0.85f,
        0.22f,
        saturate(transmittanceLuminance));
    const float3 multi = integratedScattering *
        multipleBounceWeight * 0.55f;
    gAtmosphereMultiScattering[dispatchThreadId.xy] =
        float4(max(0.0f.xxx, multi), 1.0f);
}

#endif
