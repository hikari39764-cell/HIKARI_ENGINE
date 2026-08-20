#ifndef HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED
#define HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED

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
Texture2D<float4> gAtmosphereMultiScattering : register(t1);
RWTexture2D<float4> gAtmosphereSkyView : register(u0);
SamplerState gLinearClamp : register(s0);

float3 HikariAtmosphereSafeNormalize(float3 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

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
    const float sunCosine = dot(
        viewDirection,
        HikariAtmosphereSafeNormalize(gAtmosphereSunDirection.xyz));
    const float rayleighPhase =
        0.05968310366f * (1.0f + sunCosine * sunCosine);
    const float3 multi = gAtmosphereMultiScattering.SampleLevel(
        gLinearClamp, uv, 0.0f).rgb;
    const float3 transmittance = gAtmosphereTransmittance.SampleLevel(
        gLinearClamp, float2(saturate(abs(elevation)), saturate(uv.y)), 0.0f).rgb;
    const float miePhase = 0.07957747155f *
        (1.0f - gAtmosphereMie.a * gAtmosphereMie.a) /
        pow(max(1.0e-3f, 1.0f + gAtmosphereMie.a * gAtmosphereMie.a -
            2.0f * gAtmosphereMie.a * sunCosine), 1.5f);
    const float pathLength = lerp(0.08f, 1.0f, saturate(1.0f - elevation)) *
        max(1.0f, gAtmospherePlanetParams.y - gAtmospherePlanetParams.x);
    const float3 sigma = max(
        0.0f.xxx,
        gAtmosphereRayleigh.rgb * rayleighPhase +
        gAtmosphereMie.rgb * miePhase);
    const float3 integratedScattering =
        1.0f.xxx - exp(-sigma * pathLength);
    const float aerialPerspectiveStrength =
        max(0.0f, gAtmosphereOzoneParams.y);
    const float3 radiance =
        gAtmosphereSunColorRadiance.rgb *
        gAtmosphereSunColorRadiance.a *
        integratedScattering * transmittance * aerialPerspectiveStrength +
        gAtmosphereSunColorRadiance.rgb *
        gAtmosphereSunColorRadiance.a * multi * aerialPerspectiveStrength;
    gAtmosphereSkyView[dispatchThreadId.xy] =
        float4(max(0.0f.xxx, radiance), 1.0f);
}

#endif
