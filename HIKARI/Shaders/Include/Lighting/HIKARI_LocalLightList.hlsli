#ifndef HIKARI_LOCAL_LIGHT_LIST_INCLUDED
#define HIKARI_LOCAL_LIGHT_LIST_INCLUDED

#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"

// 光源の記録の後ろにデカールの記録（HIKARI_LocalDecals.hlsli）が続く。
StructuredBuffer<HikariLocalLightGpuData>
    gHikariLocalLights : register(t0, space5);
// (index 列の先頭, 光源数 | デカール数 << 16)。index 列は光源の後ろにデカールが続く。
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

// 詰めたままのクラスタの header。記録が無いと (0, 0)。
uint2 HikariLoadLocalClusterHeader(
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

// 光源の (index 列の先頭, 数)。
uint2 HikariResolveLocalLightClusterFromDepthSlice(
    float2 pixelPosition,
    uint depthSlice)
{
    const uint2 header = HikariLoadLocalClusterHeader(pixelPosition, depthSlice);
    return uint2(header.x, header.y & 0xffffu);
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

// デカールの (index 列の先頭, 数)。光源の index の後ろから始まる。
uint2 HikariResolveLocalDecalCluster(
    float2 pixelPosition,
    float viewDepth)
{
    const uint2 header = HikariLoadLocalClusterHeader(
        pixelPosition, HikariResolveLocalLightDepthSlice(viewDepth));
    return uint2(header.x + (header.y & 0xffffu), header.y >> 16u);
}

#endif
