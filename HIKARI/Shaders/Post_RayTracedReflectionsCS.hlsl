#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Color/HIKARI_ViewExposure.hlsli"

// 光線追跡の反射。SSR の trace が外した（画面外・薄い面の裏・確信度の低い）texel だけ、実行時の加速構造へ反射光線を
// 飛ばし、当たった面を太陽・局所光・空で直接照明して trace 目標へ書く。SSR と同じ「Pre-Exposure 済みの放射輝度 +
// 確信度」なので、後段の履歴と合成はそのまま使える。空へ抜けた光線は書かず、既存の環境 fallback に任せる。
// 粗さは GGX の半ベクトルで標本化し、画素とフレームで乱数を変えて履歴で収束させる。

cbuffer RayTracedReflectionCB : register(b0)
{
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;      // 深度の大きさ（xy）と 1/大きさ（zw）
    float4 gTraceScreenParams; // trace の大きさ（xy）と 1/大きさ（zw）
    float4 gRayParams;         // x 最大距離, y 最大粗さ, z フレーム番号, w SSR を残す確信度の下限
    float4 gViewExposure;
    HikariRayLightingData gRayLighting;
};

Texture2D<float> gSceneDepth : register(t0, space9);
Texture2D<float4> gSceneSurface : register(t1, space9);
RWTexture2D<float4> gTrace : register(u0, space9);
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
SamplerState gPointClamp : register(s2);

// 法線まわりの GGX 半ベクトル（Walter 2007）。
float3 SampleGgxHalfVector(float3 normal, float roughness, float2 xi)
{
    const float alpha = max(roughness * roughness, 1.0e-3f);
    const float phi = 6.28318530718f * xi.x;
    const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
    const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    const float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gTraceScreenParams.xy)))
        return;
    const float4 current = gTrace[pixel];
    if (current.a >= gRayParams.w)
        return;
    const float2 uv = (float2(pixel) + 0.5f) * gTraceScreenParams.zw;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
        return;
    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(gSceneSurface.SampleLevel(gPointClamp, uv, 0.0f), normal, roughness, metallic);
    if (roughness > gRayParams.y)
        return;
    float3 position;
    if (!HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
        return;
    const float3 view = normalize(gCameraPosition.xyz - position);
    if (dot(view, normal) <= 0.0f)
        return;

    // 画素とフレームで変える 2 次元の乱数。
    const float frame = gRayParams.z;
    const float2 xi = float2(
        frac(52.9829189f * frac(dot(float2(pixel) + frame * 5.588238f, float2(0.06711056f, 0.00583715f)))),
        frac(52.9829189f * frac(dot(float2(pixel.yx) + frame * 3.141592f, float2(0.00583715f, 0.06711056f)))));
    const float3 halfVector = SampleGgxHalfVector(normal, roughness, xi);
    float3 direction = reflect(-view, halfVector);
    if (dot(direction, normal) <= 0.01f)
        direction = reflect(-view, normal);

    RayDesc ray;
    ray.Origin = position + normal * 0.01f;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = max(gRayParams.x, 0.1f);
    HikariRaySurface hit;
    if (!HikariTraceRuntimeScene(ray, gRayLighting.scene.x, gRayLighting.scene.y, gRayLighting.time, gMaterialWrap, 0u,
            hit))
        return;
    // 当たった面の拡散の入射光は、面の法線方向の空（ぼかした mip）で近似する。
    const float3 incidentDiffuse = HikariRayEnvironmentRadiance(gRayLighting, hit.normal, gLinearClamp, 4.0f);
    const float3 radiance = HikariRayHitRadiance(gRayLighting, hit, -direction, xi.yx, incidentDiffuse, true,
        gMaterialWrap);
    gTrace[pixel] = float4(HikariPreExpose(radiance, gViewExposure), 1.0f);
}
