#ifndef HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED
#define HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED

cbuffer AtmosphereLutCB : register(b0)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams;
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
};

Texture2D<float4> gAtmosphereTransmittance : register(t0);
RWTexture2D<float4> gAtmosphereMultiScattering : register(u0);
SamplerState gLinearClamp : register(s0);

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
    const float3 transmittance = gAtmosphereTransmittance.SampleLevel(
        gLinearClamp,
        uv, 0.0f).rgb;
    const float opticalDepth = 1.0f - dot(
        transmittance, float3(0.2126f, 0.7152f, 0.0722f));
    const float phase = 0.25f + 0.75f * saturate(uv.y);
    const float pathLength = lerp(0.05f, 0.65f, saturate(uv.y)) *
        max(1.0f, gAtmospherePlanetParams.y - gAtmospherePlanetParams.x);
    const float3 sigma = max(
        0.0f.xxx,
        gAtmosphereRayleigh.rgb + gAtmosphereMie.rgb);
    const float3 integratedScattering =
        1.0f.xxx - exp(-sigma * pathLength);
    const float3 multi = integratedScattering *
        (0.25f + 0.75f * saturate(1.0f - opticalDepth)) * phase;
    gAtmosphereMultiScattering[dispatchThreadId.xy] =
        float4(max(0.0f.xxx, multi), 1.0f);
}

#endif
