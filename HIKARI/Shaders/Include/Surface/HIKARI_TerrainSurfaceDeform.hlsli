#ifndef HIKARI_TERRAIN_SURFACE_DEFORM_INCLUDED
#define HIKARI_TERRAIN_SURFACE_DEFORM_INCLUDED

#include "Include/Surface/HIKARI_TerrainSurfaceParams.hlsli"

// 地形の頂点変形。最終高さと勾配 (ripple・縁の沈降・低ポリ面込み) は CPU が
// 格子へ焼き込み済みなので、全 pass が同じ値を読むだけでよい。
void HikariApplyTerrainSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 terrainSurfaceData)
{
    terrainSurfaceData = float3(localPosition.xz, 0.0f);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE) == 0u)
    {
        return;
    }

    const HikariTerrainSurfaceParams p =
        HikariLoadTerrainSurfaceParams(instance.surfaceFeatureDataIndex);
    const float2 uv = localPosition.xz / p.extents + 0.5f;
    const float2 slope = HikariTerrainSampleSlope(p, uv);
    localPosition.y += HikariTerrainSampleHeight(p, uv);
    localNormal = normalize(float3(-slope.x, 1.0f, -slope.y));
    // 着色へ渡すのは runtime 応答だけ。負値が pit、正値が押し出された rim。
    terrainSurfaceData = float3(localPosition.xz, HikariTerrainSampleDeformation(p, uv));
}

#endif
