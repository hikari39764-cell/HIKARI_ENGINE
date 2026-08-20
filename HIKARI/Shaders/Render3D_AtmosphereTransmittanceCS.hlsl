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
    float4 gAtmosphereOzoneParams;   // width, aerial perspective, exposure, albedo
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
};

RWTexture2D<float4> gAtmosphereTransmittance : register(u0);

float HikariAtmosphereDensity(float height, float scaleHeight)
{
    return exp(-max(0.0f, height) / max(1.0e-4f, scaleHeight));
}

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
    const float altitude = uv.y *
        max(1.0f, gAtmospherePlanetParams.y - gAtmospherePlanetParams.x);
    const float rayleighDensity = HikariAtmosphereDensity(
        altitude, gAtmospherePlanetParams.z);
    const float mieDensity = HikariAtmosphereDensity(
        altitude, gAtmospherePlanetParams.w);
    const float ozoneBand = exp(-abs(
        altitude - gAtmosphereOzone.a) /
        max(1.0e-4f, gAtmosphereOzoneParams.x));
    const float3 extinction =
        gAtmosphereRayleigh.rgb * rayleighDensity +
        gAtmosphereMie.rgb * mieDensity +
        gAtmosphereOzone.rgb * ozoneBand;
    const float pathLength = lerp(0.02f, 1.0f, uv.x) *
        max(1.0f, gAtmospherePlanetParams.y - gAtmospherePlanetParams.x);
    const float3 transmittance = exp(-max(0.0f.xxx, extinction * pathLength));
    gAtmosphereTransmittance[dispatchThreadId.xy] =
        float4(saturate(transmittance), 1.0f);
}

#endif
