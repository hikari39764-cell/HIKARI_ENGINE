#ifndef HIKARI_SAND_SURFACE_DEFORM_INCLUDED
#define HIKARI_SAND_SURFACE_DEFORM_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"

float HikariSandSampleSurfaceHeight(
    HikariSandSurfaceParams p,
    float2 localXZ)
{
    const float2 uv =
        localXZ / p.extents + 0.5f;
    const float deform = HikariSandSampleHeight(p, uv);
    const float base = HikariEvaluateSandBaseHeight(
        localXZ,
        p.rippleAmount,
        p.heightScale);
    return base + deform;
}

void HikariApplySandSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 sandSurfaceData)
{
    sandSurfaceData = float3(localPosition.xz, 0.0f);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_SAND_SURFACE) == 0u)
    {
        return;
    }

    const HikariSandSurfaceParams p =
        HikariLoadSandSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);
    const float cellX = p.extents.x / (float)max(p.gridResolution, 1u);
    const float cellZ = p.extents.y / (float)max(p.gridResolution, 1u);

    const float2 uv =
        localPosition.xz / p.extents + 0.5f;
    const float du = cellX / p.extents.x;
    const float dv = cellZ / p.extents.y;

    const float hL = HikariSandSampleHeight(p, uv - float2(du, 0.0f));
    const float hR = HikariSandSampleHeight(p, uv + float2(du, 0.0f));
    const float hD = HikariSandSampleHeight(p, uv - float2(0.0f, dv));
    const float hU = HikariSandSampleHeight(p, uv + float2(0.0f, dv));
    const float slopeX = (hR - hL) / (2.0f * cellX);
    const float slopeZ = (hU - hD) / (2.0f * cellZ);

    const float totalHeight = HikariSandSampleSurfaceHeight(
        p,
        localPosition.xz);
    localPosition.y += totalHeight;
    localNormal = normalize(float3(-slopeX, 1.0f, -slopeZ));
    sandSurfaceData = float3(localPosition.xz, totalHeight);
}

#endif
