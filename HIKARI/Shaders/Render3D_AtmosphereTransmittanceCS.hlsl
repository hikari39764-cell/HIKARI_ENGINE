#ifndef HIKARI_ATMOSPHERE_TRANSMITTANCE_CS_INCLUDED
#define HIKARI_ATMOSPHERE_TRANSMITTANCE_CS_INCLUDED

// Runtime LUT pass dispatched by AtmosphereLutOwner. The owner supplies UAV
// allocation, state transitions, and frame-fenced resource replacement.
#define HIKARI_ATMOSPHERE_DECLARE_LUT_CB


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

    const float2 uv = float2(dispatchThreadId.xy) / float2(max(1u, width-1u), max(1u, height-1u));
    const float radius = HikariAtmosphereLutRadius(uv.y);
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
