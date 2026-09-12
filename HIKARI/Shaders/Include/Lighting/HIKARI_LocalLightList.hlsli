#ifndef HIKARI_LOCAL_LIGHT_LIST_INCLUDED
#define HIKARI_LOCAL_LIGHT_LIST_INCLUDED

#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"

StructuredBuffer<HikariLocalLightGpuData>
    gHikariLocalLights : register(t0, space5);
StructuredBuffer<uint2>
    gHikariLocalLightClusterHeaders : register(t1, space5);
StructuredBuffer<uint>
    gHikariLocalLightClusterIndices : register(t2, space5);

uint HikariResolveLocalLightDepthSliceFromLogDistance(
    float logDistance)
{
    if (gLocalLightClusterCountZ == 0u)
    {
        return 0u;
    }
    const float normalized = saturate(
        logDistance * gLocalLightDepthScale +
            gLocalLightDepthBias);
    return min(
        (uint)(normalized * (float)gLocalLightClusterCountZ),
        gLocalLightClusterCountZ - 1u);
}

uint HikariResolveLocalLightDepthSlice(float viewDepth)
{
    return HikariResolveLocalLightDepthSliceFromLogDistance(
        log(max(viewDepth, 0.0001f)));
}

uint HikariResolveLocalLightClusterIndex(
    float2 pixelPosition,
    uint depthSlice)
{
    const uint tileSize = max(gLocalLightTileSizePixels, 1u);
    const uint tileX = min(
        (uint)pixelPosition.x / tileSize,
        gLocalLightClusterCountX - 1u);
    const uint tileY = min(
        (uint)pixelPosition.y / tileSize,
        gLocalLightClusterCountY - 1u);
    const uint tileZ = min(
        depthSlice,
        gLocalLightClusterCountZ - 1u);
    return (tileZ * gLocalLightClusterCountY + tileY) *
        gLocalLightClusterCountX + tileX;
}

uint2 HikariResolveLocalLightClusterFromDepthSlice(
    float2 pixelPosition,
    uint depthSlice)
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
            depthSlice)];
}

uint2 HikariResolveLocalLightCluster(
    float2 pixelPosition,
    float viewDepth)
{
    return HikariResolveLocalLightClusterFromDepthSlice(
        pixelPosition,
        HikariResolveLocalLightDepthSlice(
            viewDepth));
}

#endif
