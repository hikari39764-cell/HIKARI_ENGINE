#ifndef HIKARI_VOLUMETRIC_CAMERA_INCLUDED
#define HIKARI_VOLUMETRIC_CAMERA_INCLUDED

bool HikariIsOrthographic(float4x4 viewProjection)
{
    // 射影行列と逆行列のどちらも、直交投影では最終行の xyz が 0 になる。
    return dot(viewProjection[3].xyz, viewProjection[3].xyz) < 1.0e-12f;
}

float3 HikariOrthographicForwardFromInverseViewProjection(float4x4 inverseViewProjection)
{
    // HLSL は row-first indexing。逆行列の第 3 列が clip-Z に対する world の微分。
    const float3 depthAxis = float3(
        inverseViewProjection[0][2],
        inverseViewProjection[1][2],
        inverseViewProjection[2][2]);
    return normalize(depthAxis);
}

float3 HikariUnprojectVolumePosition(float2 uv, float depth, float4x4 inverseViewProjection)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 world = mul(inverseViewProjection, clip);
    return world.xyz / max(abs(world.w), 1.0e-6f);
}

void HikariReconstructVolumeRay(
    float2 uv,
    float4x4 inverseViewProjection,
    float3 cameraPosition,
    out float3 rayOrigin,
    out float3 rayDirection,
    out bool orthographic)
{
    orthographic = HikariIsOrthographic(inverseViewProjection);
    if (orthographic)
    {
        rayDirection = HikariOrthographicForwardFromInverseViewProjection(inverseViewProjection);
        const float3 nearPosition = HikariUnprojectVolumePosition(uv, 0.0f, inverseViewProjection);
        rayOrigin = nearPosition - rayDirection * dot(nearPosition - cameraPosition, rayDirection);
        return;
    }

    rayOrigin = cameraPosition;
    const float3 farPosition = HikariUnprojectVolumePosition(uv, 1.0f, inverseViewProjection);
    rayDirection = normalize(farPosition - cameraPosition);
}

float3 HikariVolumeCameraForward(float4x4 inverseViewProjection, float3 cameraPosition, bool orthographic)
{
    if (orthographic)
    {
        return HikariOrthographicForwardFromInverseViewProjection(inverseViewProjection);
    }

    const float3 centerPosition = HikariUnprojectVolumePosition(
        float2(0.5f, 0.5f), 1.0f, inverseViewProjection);
    return normalize(centerPosition - cameraPosition);
}

float HikariVolumeCameraDistance(
    float3 worldPosition,
    float3 cameraPosition,
    float3 cameraForward,
    bool orthographic)
{
    const float3 offset = worldPosition - cameraPosition;
    return orthographic ? max(dot(offset, cameraForward), 0.0f) : length(offset);
}

float HikariVolumeViewDepth(float3 worldPosition, float3 cameraPosition, float3 cameraForward)
{
    return max(dot(worldPosition - cameraPosition, cameraForward), 0.0f);
}

// froxel の奥行きの分布。fraction は near..far を 0..1 とし、log(1 + d / knee) に比例させる。カメラの近く（knee 以内）は
// 等間隔に近く、遠くは対数になる。純粋な対数（near からの比）だと near が 0.01m の時に半分の層が 1m 以内に集まり、
// 部屋の大きさの光の筋が数層にしか乗らない。
static const float HIKARI_VOLUME_DEPTH_KNEE = 2.0f;

float HikariVolumeDepthFraction(float distanceToCamera, float nearDistance, float farDistance)
{
    const float safeNear = max(0.01f, nearDistance);
    const float safeFar = max(safeNear + 0.01f, farDistance);
    return saturate(log(1.0f + max(distanceToCamera - safeNear, 0.0f) / HIKARI_VOLUME_DEPTH_KNEE) /
        log(1.0f + (safeFar - safeNear) / HIKARI_VOLUME_DEPTH_KNEE));
}

float HikariVolumeFractionDistance(float fraction, float nearDistance, float farDistance)
{
    const float safeNear = max(0.01f, nearDistance);
    const float safeFar = max(safeNear + 0.01f, farDistance);
    return safeNear + HIKARI_VOLUME_DEPTH_KNEE *
        (exp(fraction * log(1.0f + (safeFar - safeNear) / HIKARI_VOLUME_DEPTH_KNEE)) - 1.0f);
}

float HikariVolumeDistanceToSlice(float distanceToCamera, float nearDistance, float farDistance, float depthSlices)
{
    // 最初/最後の格納点は near/far 境界。Texture の Texel 中心へ写像する。
    return (HikariVolumeDepthFraction(distanceToCamera, nearDistance, farDistance) * (depthSlices - 1.0f) + 0.5f) /
        depthSlices;
}

#endif
