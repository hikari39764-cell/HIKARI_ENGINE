#ifndef HIKARI_SCENE_SURFACE_CONTRACT_INCLUDED
#define HIKARI_SCENE_SURFACE_CONTRACT_INCLUDED

// Shared packed surface contract:
//   RG: octahedral world-space normal
//   B : perceptual roughness
//   A : metallic
#include "../HIKARI_DirectionEncoding.hlsli"

// 遠景模型の面の印（FarModelBake が材質へ書く金属度。実質 0）。面が木の中心にあるので、太陽の影の光線は大量配置を見ない。
static const float HIKARI_FAR_MODEL_METALLIC = 1.0f / 255.0f;

bool HikariIsFarModelSurface(float metallic)
{
    return abs(metallic - HIKARI_FAR_MODEL_METALLIC) < 0.001f;
}

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
