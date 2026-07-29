#ifndef HIKARI_LOCAL_LIGHT_LIST_INCLUDED
#define HIKARI_LOCAL_LIGHT_LIST_INCLUDED

#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"

StructuredBuffer<HikariLocalLightGpuData>
    gHikariLocalLights : register(t0, space5);
StructuredBuffer<uint2>
    gHikariLocalLightClusterHeaders : register(t1, space5);
StructuredBuffer<uint>
    gHikariLocalLightClusterIndices : register(t2, space5);

uint HikariResolveLocalLightDepthSlice(float3 worldPosition)
{
    const float nearDistance = max(gLocalLightNearDistance, 0.01f);
    const float farDistance = max(
        gLocalLightFarDistance,
        nearDistance + 0.01f);
    const float distanceToCamera = clamp(
        length(worldPosition - gCameraPos.xyz),
        nearDistance,
        farDistance);
    const float denominator = log(farDistance / nearDistance);
    const float normalized = denominator > 0.0001f
        ? log(distanceToCamera / nearDistance) / denominator
        : 0.0f;
    return min(
        (uint)(normalized * (float)gLocalLightClusterCountZ),
        gLocalLightClusterCountZ - 1u);
}

uint HikariResolveLocalLightClusterIndex(
    float2 pixelPosition,
    float3 worldPosition)
{
    const uint tileSize = max(gLocalLightTileSizePixels, 1u);
    const uint tileX = min(
        (uint)pixelPosition.x / tileSize,
        gLocalLightClusterCountX - 1u);
    const uint tileY = min(
        (uint)pixelPosition.y / tileSize,
        gLocalLightClusterCountY - 1u);
    const uint tileZ =
        HikariResolveLocalLightDepthSlice(worldPosition);
    return (tileZ * gLocalLightClusterCountY + tileY) *
        gLocalLightClusterCountX + tileX;
}

uint2 HikariResolveLocalLightCluster(
    float2 pixelPosition,
    float3 worldPosition)
{
    if (gLocalLightCount == 0u ||
        gLocalLightClusterCountX == 0u ||
        gLocalLightClusterCountY == 0u ||
        gLocalLightClusterCountZ == 0u)
    {
        return uint2(0u, 0u);
    }
    return gHikariLocalLightClusterHeaders[
        HikariResolveLocalLightClusterIndex(
            pixelPosition,
            worldPosition)];
}

#endif
