#ifndef HIKARI_SCENE_SURFACE_CONTRACT_INCLUDED
#define HIKARI_SCENE_SURFACE_CONTRACT_INCLUDED

// Shared packed surface contract:
//   RG: octahedral world-space normal
//   B : perceptual roughness
//   A : metallic
#include "../HIKARI_DirectionEncoding.hlsli"

float4 HikariPackSceneSurface(
    float3 normal,
    float roughness,
    float metallic)
{
    return float4(
        HikariEncodeOctahedralDirection(normalize(normal)),
        saturate(roughness),
        saturate(metallic));
}

void HikariUnpackSceneSurface(
    float4 packed,
    out float3 normal,
    out float roughness,
    out float metallic)
{
    normal = HikariDecodeOctahedralDirection(packed.rg);
    roughness = packed.b;
    metallic = packed.a;
}

#endif
