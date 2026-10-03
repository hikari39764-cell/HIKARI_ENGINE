#ifndef HIKARI_LOCAL_FOG_DENSITY_INCLUDED
#define HIKARI_LOCAL_FOG_DENSITY_INCLUDED

#ifndef HIKARI_NOISE_INCLUDED
#include "Include/Math/HIKARI_Noise.hlsli"
#endif

struct HikariLocalFogVolume
{
    float4x4 worldToBox;
    float4 colorDensity;
    float4 noiseEdgeScattering;
    float4 noiseOffset;
};

float HikariLocalFogExtinction(HikariLocalFogVolume volume, float3 worldPosition)
{
    const float3 local = mul(volume.worldToBox, float4(worldPosition, 1.0f)).xyz;
    const float3 distanceToFace = 1.0f - abs(local) * 2.0f;
    const float interior = min(distanceToFace.x, min(distanceToFace.y, distanceToFace.z));
    if (interior <= 0.0f) return 0.0f;
    const float edge = volume.noiseEdgeScattering.z;
    const float fade = edge > 0.0f ? smoothstep(0.0f, edge, interior) : 1.0f;
    float modulation = 1.0f;
    if (volume.noiseEdgeScattering.y > 0.0f)
    {
        const float3 position = (worldPosition + volume.noiseOffset.xyz) * volume.noiseEdgeScattering.x;
        const float noise = HikariNoisePeriodicValue3D(position, int3(256, 256, 256), 0x31a7c9edu);
        modulation = lerp(1.0f, noise, volume.noiseEdgeScattering.y);
    }
    return volume.colorDensity.a * fade * modulation;
}

#endif
