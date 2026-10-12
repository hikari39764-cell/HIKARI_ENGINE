#ifndef HIKARI_SCREEN_SPACE_GEOMETRY_INCLUDED
#define HIKARI_SCREEN_SPACE_GEOMETRY_INCLUDED

#include "Include/Math/HIKARI_FiniteMath.hlsli"

// Shared projection and SceneDepth semantics for screen-space consumers.
// Matrices use HIKARI's zero-to-one NDC depth convention.

bool HikariIsValidSceneDepth(float depth)
{
    return HikariIsFiniteFloat(depth) &&
        depth > 0.0f && depth < 0.99999f;
}

float2 HikariScreenUvFromSvPosition(
    float4 svPosition,
    float2 inverseScreenSize)
{
    return svPosition.xy * inverseScreenSize;
}

// uv を点で読む texel の中心へ寄せる（texelParams は xy 大きさ、zw その逆数）。低い解像度の画素の中心は高い解像度の
// texel の境目に来るので、そのまま点で読むと、読んだ深度の texel と位置の復元に使う uv が半 texel ずれ、斜めの面で
// 復元した位置が面から浮き沈みする。境目ちょうどの uv は丸め誤差でどちらの texel にも転ぶので、わずかに正の側へ寄せて
// 毎回同じ texel を取る。
float2 HikariSnapUvToTexelCenter(float2 uv, float4 texelParams)
{
    return (floor(uv * texelParams.xy + 0.01f) + 0.5f) * texelParams.zw;
}

bool HikariIsScreenUvInsideTexelGuard(
    float2 uv,
    float2 inverseScreenSize)
{
    const float2 guard = inverseScreenSize * 0.5f;
    return all(uv >= guard) && all(uv <= 1.0f.xx - guard);
}

bool HikariReconstructWorldPosition(
    float2 uv,
    float depth,
    float4x4 inverseViewProjection,
    out float3 worldPosition)
{
    if (!HikariIsValidSceneDepth(depth))
    {
        worldPosition = 0.0f.xxx;
        return false;
    }
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    const float4 world =
        mul(inverseViewProjection, float4(ndc, depth, 1.0f));
    if (!HikariIsFiniteFloat4(world) || abs(world.w) <= 1.0e-6f)
    {
        worldPosition = 0.0f.xxx;
        return false;
    }
    worldPosition = world.xyz / world.w;
    return HikariIsFiniteFloat3(worldPosition);
}

// 隣の texel の位置との差と、その長さの二乗（画面外・深度が無い時は大きな値）。
float4 HikariSceneDepthNeighborDelta(Texture2D<float> sceneDepth, SamplerState pointClamp, float2 uv, float2 offset,
    float4x4 inverseViewProjection, float3 position)
{
    const float2 sampleUv = uv + offset;
    float3 neighbor;
    if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f) ||
        !HikariReconstructWorldPosition(sampleUv, sceneDepth.SampleLevel(pointClamp, sampleUv, 0.0f),
            inverseViewProjection, neighbor))
        return float4(0.0f, 0.0f, 0.0f, 1.0e30f);
    const float3 delta = neighbor - position;
    return float4(delta, dot(delta, delta));
}

// 深度の差分から求める実際の面（多角形）の法線。左右・上下それぞれ近い方の隣を使い、輪郭で奥や手前の物を拾わない
// （片側の差分だけだと球の下端で床の texel を拾って法線が傾き、遮蔽が斑になった）。見ている側へ向ける。
float3 HikariNormalFromSceneDepth(Texture2D<float> sceneDepth, SamplerState pointClamp, float2 uv, float3 position,
    float2 texel, float4x4 inverseViewProjection, float3 cameraPosition)
{
    const float4 right = HikariSceneDepthNeighborDelta(sceneDepth, pointClamp, uv, float2(texel.x, 0.0f),
        inverseViewProjection, position);
    const float4 left = HikariSceneDepthNeighborDelta(sceneDepth, pointClamp, uv, float2(-texel.x, 0.0f),
        inverseViewProjection, position);
    const float4 down = HikariSceneDepthNeighborDelta(sceneDepth, pointClamp, uv, float2(0.0f, texel.y),
        inverseViewProjection, position);
    const float4 up = HikariSceneDepthNeighborDelta(sceneDepth, pointClamp, uv, float2(0.0f, -texel.y),
        inverseViewProjection, position);
    const float3 dx = right.w <= left.w ? right.xyz : -left.xyz;
    const float3 dy = down.w <= up.w ? down.xyz : -up.xyz;
    const float3 normal = cross(dy, dx);
    const float lengthSquared = dot(normal, normal);
    const float3 toCamera = normalize(cameraPosition - position);
    if (lengthSquared < 1.0e-12f)
        return toCamera;
    const float3 unit = normal * rsqrt(lengthSquared);
    return dot(unit, toCamera) < 0.0f ? -unit : unit;
}

bool HikariReconstructViewDepth(
    float2 uv,
    float depth,
    float4x4 inverseViewProjection,
    out float viewDepth)
{
    if (!HikariIsValidSceneDepth(depth))
    {
        viewDepth = 0.0f;
        return false;
    }
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    const float4 world =
        mul(inverseViewProjection, float4(ndc, depth, 1.0f));
    if (!HikariIsFiniteFloat4(world) || abs(world.w) <= 1.0e-6f)
    {
        viewDepth = 0.0f;
        return false;
    }
    viewDepth = rcp(abs(world.w));
    return HikariIsFiniteFloat(viewDepth) && viewDepth > 0.0f;
}

bool HikariProjectWorldPosition(
    float3 worldPosition,
    float4x4 viewProjection,
    out float2 uv,
    out float projectedDepth,
    out float viewDepth)
{
    const float4 clip =
        mul(viewProjection, float4(worldPosition, 1.0f));
    if (!HikariIsFiniteFloat4(clip) || clip.w <= 1.0e-5f)
    {
        uv = 0.0f.xx;
        projectedDepth = 1.0f;
        viewDepth = 0.0f;
        return false;
    }
    const float3 ndc = clip.xyz / clip.w;
    uv = float2(
        ndc.x * 0.5f + 0.5f,
        -ndc.y * 0.5f + 0.5f);
    projectedDepth = ndc.z;
    viewDepth = clip.w;
    return HikariIsFiniteFloat3(ndc) &&
        all(uv > 0.0f.xx) && all(uv < 1.0f.xx) &&
        projectedDepth > 0.0f && projectedDepth < 1.0f;
}

#endif
