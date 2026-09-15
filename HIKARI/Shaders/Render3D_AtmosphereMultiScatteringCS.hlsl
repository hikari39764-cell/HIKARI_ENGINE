#ifndef HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED
#define HIKARI_ATMOSPHERE_MULTI_SCATTERING_CS_INCLUDED

#define HIKARI_ATMOSPHERE_DECLARE_LUT_CB


Texture2D<float4> gAtmosphereTransmittance : register(t0);
RWTexture2D<float4> gAtmosphereMultiScattering : register(u0);
SamplerState gLinearClamp : register(s0);

#define HIKARI_ATMOSPHERE_USE_TRANSMITTANCE_LUT
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

    const float2 uv = float2(dispatchThreadId.xy) / float2(max(1u, width-1u), max(1u, height-1u));
    const float3 origin = HikariAtmospherePositionFromRadius(HikariAtmosphereLutRadius(uv.y));
    const float3 sunDirection = HikariAtmosphereDirectionFromMu(uv.x * 2.0f - 1.0f);
    float3 meanRadiance = 0.0f.xxx;
    float3 meanTransfer = 0.0f.xxx;
    // Hillaire 2020: 球面平均の isotropic transfer による高次散乱。
    // 32 方向 x 24 区間。太陽方向の内部 ray march は既存 transmittance LUT に置換。
    const uint directionCount = 32u;
    [loop]
    for (uint index = 0; index < directionCount; ++index)
    {
        const float mu = 1.0f - 2.0f * (float(index) + 0.5f) / float(directionCount);
        const float azimuth = float(index) * 2.39996323f;
        const float horizontal = sqrt(max(0.0f, 1.0f - mu*mu));
        const float3 direction = float3(horizontal*cos(azimuth), mu, horizontal*sin(azimuth));
        const HikariAtmosphereViewIntegration path = HikariAtmosphereIntegrateSegment(
            origin, direction, sunDirection, HIKARI_ATMOSPHERE_MAX_SKY_STEPS, 0u, 1e30f, true);
        meanRadiance += path.scattering;
        meanTransfer += path.isotropicTransfer;
        if (path.groundDistance > 0.0f)
        {
            const float3 ground = origin + direction * path.groundDistance;
            const float3 normal = HikariAtmosphereSafeNormalize(ground);
            const float3 albedo = saturate(gAtmosphereGroundColor.rgb) * saturate(gAtmosphereOzoneParams.w);
            const float3 throughput = path.transmittance * albedo;
            meanRadiance += throughput * saturate(dot(normal, sunDirection)) *
                HikariAtmosphereIntegrationSunTransmittance(ground + normal, sunDirection, 0u) /
                HIKARI_ATMOSPHERE_PI;
            meanTransfer += throughput;
        }
    }
    meanRadiance /= float(directionCount);
    meanTransfer /= float(directionCount);
    // 密な保守媒体でも数値誤差で級数が発散しない。
    const float3 multi = meanRadiance / max(1.0f - meanTransfer, 0.001f);
    gAtmosphereMultiScattering[dispatchThreadId.xy] = float4(
        all(isfinite(multi)) ? clamp(multi, 0.0f, 65000.0f) : 0.0f.xxx, 1.0f);
}

#endif
