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

// 近くの自分自身の遮りを自分の shader で求める面の印（金属度へ書く。実質 0）。光線追跡の代理が面そのものより粗い面
// （地形）が使う。低い太陽では、面と代理の数 cm の違いで影の光線の当たり外れが面ごと入れ替わり、視点が動くたびに影が
// 明滅した。太陽の影の光線は、この印の面からは HIKARI_OWN_NEAR_SHADOW_METERS より先でだけ代理を見る（場面の物は起点
// から見る）。その距離までの自分自身の遮りは、面の Forward の shader が自分のデータで求めて、影の値に掛ける。
static const float HIKARI_OWN_NEAR_SHADOW_METALLIC = 2.0f / 255.0f;
static const float HIKARI_OWN_NEAR_SHADOW_METERS = 16.0f;

bool HikariHasOwnNearShadow(float metallic)
{
    return abs(metallic - HIKARI_OWN_NEAR_SHADOW_METALLIC) < 0.001f;
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
