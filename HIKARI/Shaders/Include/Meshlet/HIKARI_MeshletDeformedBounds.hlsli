#ifndef HIKARI_MESHLET_DEFORMED_BOUNDS_INCLUDED
#define HIKARI_MESHLET_DEFORMED_BOUNDS_INCLUDED

#include "Include/Surface/HIKARI_TerrainSurfaceParams.hlsli"

// shadow route は水面 record (t0, space7) を束縛しない。
#ifndef HIKARI_MESHLET_AS_WATER_BOUNDS
#define HIKARI_MESHLET_AS_WATER_BOUNDS 1
#endif

#if HIKARI_MESHLET_AS_WATER_BOUNDS
#include "Include/HIKARI_WaterSurfaceParams.hlsli"
#include "Include/Meshlet/HIKARI_OceanGeometry.hlsli"
#endif

// 頂点変形する地形と水面は、平面 cluster の local bounds を変形包絡で
// 広げて AS 剔除に参加させる。包絡を確定できない面は false (可視側) を返す。
// 相机を追う海は、格子を相机の前へ移して歪める（HikariApplyWaterSurfaceDeform と同じ計算）。
// 歪めは各軸で単調なので、簇の箱の角を写せば変形後の箱になる。invViewProj と cameraPosition は主視点。
bool HikariMeshletResolveDeformedClusterBounds(
    HikariSurfaceGpuSceneInstance instance,
    float4x4 invViewProj,
    float4 cameraPosition,
    inout float4 boundsMin,
    inout float4 boundsMax)
{
    const uint unsupported =
        HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
        HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND;
    if ((instance.flags & unsupported) != 0u)
    {
        return false;
    }
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_TERRAIN_SURFACE) != 0u)
    {
        // 地形は平面格子の Y を焼き込み高さだけ動かす。CPU の保守範囲を足す。
        const HikariTerrainSurfaceParams p =
            HikariLoadTerrainSurfaceParams(instance.surfaceFeatureDataIndex);
        if (!p.valid || !(p.boundsMinY <= p.boundsMaxY))
        {
            return false;
        }
        boundsMin.y += p.boundsMinY;
        boundsMax.y += p.boundsMaxY;
        return true;
    }
#if HIKARI_MESHLET_AS_WATER_BOUNDS
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE) != 0u)
    {
        const HikariWaterSurfaceParams p =
            HikariLoadWaterSurfaceParamsFromDataIndex(instance.surfaceFeatureDataIndex);
        float2 envelope = HikariWaterSurfaceCarrierEnvelope(p);
        if (HikariWaterUsesOceanSpectrum(p))
        {
            // 頻譜残差は world 変位を local へ写して足される。
            envelope.x += HIKARI_OCEAN_SPECTRUM_MAX_HORIZONTAL_DISPLACEMENT *
                HIKARI_WATER_SPECTRUM_MAX_HORIZONTAL_SCALE *
                HIKARI_WATER_SPECTRUM_RESIDUAL_WEIGHT *
                HikariOceanWorldDeltaToLocalScaleBound(instance.clusterWorld);
            envelope.y += HIKARI_OCEAN_SPECTRUM_MAX_VERTICAL_DISPLACEMENT *
                HIKARI_WATER_SPECTRUM_RESIDUAL_WEIGHT;
        }
        if (!all(isfinite(envelope)))
        {
            return false;
        }
        if (p.cameraCenteredOcean)
        {
            const float2 halfExtent = HikariOceanSafeHalfExtent(
                HikariOceanResolveAuthoredHalfExtent(p.authoredHalfExtentXZ, instance.boundsCenterRadius.w,
                    instance.world),
                1.0f.xx);
            const float2 uniformCell = HikariOceanResolveUniformCell(halfExtent, instance.clusterIndexCount);
            const float share = HikariOceanGradeShare(uniformCell);
            const float2 center = HikariOceanSnapCameraXZ(
                HikariOceanWorldToLocalXZ(instance.world,
                    HikariOceanResolveGridCenterWorldXZ(instance.world, invViewProj, cameraPosition)),
                HikariOceanResolveSnapCell(halfExtent, instance.clusterIndexCount));
            const float2 low = clamp((boundsMin.xz - p.authoredCenterXZ) / halfExtent, -1.0f.xx, 1.0f.xx);
            const float2 high = clamp((boundsMax.xz - p.authoredCenterXZ) / halfExtent, -1.0f.xx, 1.0f.xx);
            boundsMin.xz = center + halfExtent * HikariOceanGrade(low, share);
            boundsMax.xz = center + halfExtent * HikariOceanGrade(high, share);
            // 波は world の変位を local へ写して足す。
            envelope.x *= HikariOceanWorldDeltaToLocalScaleBound(instance.world);
            if (!all(isfinite(boundsMin.xz)) || !all(isfinite(boundsMax.xz)))
            {
                return false;
            }
        }
        boundsMin.xyz -= float3(envelope.x, envelope.y, envelope.x);
        boundsMax.xyz += float3(envelope.x, envelope.y, envelope.x);
        return true;
    }
#endif
    return false;
}

#endif
