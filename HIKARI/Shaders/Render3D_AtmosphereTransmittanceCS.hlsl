#ifndef HIKARI_ATMOSPHERE_TRANSMITTANCE_CS_INCLUDED
#define HIKARI_ATMOSPHERE_TRANSMITTANCE_CS_INCLUDED

// Runtime LUT pass dispatched by AtmosphereLutOwner. The owner supplies UAV
// allocation, state transitions, and frame-fenced resource replacement.
cbuffer AtmosphereLutCB : register(b0)
{
    float4 gAtmospherePlanetParams; // radius, top radius, Rayleigh H, Mie H
    float4 gAtmosphereRayleigh;     // scattering RGB, sun angular radius
    float4 gAtmosphereMie;          // scattering RGB, anisotropy
    float4 gAtmosphereOzone;        // absorption RGB, center height
    float4 gAtmosphereOzoneParams;   // width, aerial perspective, observer sun T, albedo
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance; // source color, source radiance
};

RWTexture2D<float4> gAtmosphereTransmittance : register(u0);

#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0u;
    uint height = 0u;
    gAtmosphereTransmittance.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) /
        float2(max(1u, width), max(1u, height));
    // X is mu (view angle), Y is normalized radius. This is the usual
    // (r, mu) transmittance parameterization while retaining the existing
    // 256x64 texture orientation.
    const float radius = lerp(
        HikariAtmospherePlanetRadius() + 0.5f,
        HikariAtmosphereTopRadius() - 0.5f,
        uv.y);
    const float mu = uv.x * 2.0f - 1.0f;
    const float3 origin = HikariAtmospherePositionFromRadius(radius);
    const float3 direction = HikariAtmosphereDirectionFromMu(mu);
    const float distanceToTop = HikariAtmosphereDistanceToTop(origin, direction);
    const float distanceToGround = HikariAtmosphereDistanceToGround(
        origin + direction * 1.0e-2f,
        direction);
    const bool blockedByGround = distanceToGround > 0.0f &&
        distanceToGround < distanceToTop;
    const float3 transmittance = blockedByGround || distanceToTop <= 0.0f
        ? 0.0f.xxx
        : exp(-HikariAtmosphereIntegrateOpticalDepth(
            origin,
            direction,
            distanceToTop,
            HIKARI_ATMOSPHERE_MAX_OPTICAL_STEPS));
    gAtmosphereTransmittance[dispatchThreadId.xy] =
        float4(saturate(max(0.0f.xxx, transmittance)), 1.0f);
}

#endif
