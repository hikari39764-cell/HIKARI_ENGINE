#ifndef HIKARI_WATER_SURFACE_DEFORM_INCLUDED
#define HIKARI_WATER_SURFACE_DEFORM_INCLUDED

#ifndef HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM
#define HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM 1
#endif

#if HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM

#include "Include/HIKARI_WaterSurfaceParams.hlsli"

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    waterSurfaceData = float3(localPosition.xz, 0.0f);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE) == 0u)
    {
        return;
    }

    const float3 sourceLocalPosition = localPosition;
    const HikariWaterSurfaceParams p =
        HikariLoadWaterSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);
    const HikariWaterSurfaceEvaluation evaluation =
        HikariEvaluateWaterSurface(p, sourceLocalPosition, simulationTime);
    localPosition = evaluation.positionLocal;
    localNormal = evaluation.normalLocal;
    waterSurfaceData = float3(sourceLocalPosition.xz, evaluation.compression);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

#else

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    (void)instance;
    (void)simulationTime;
    (void)localNormal;
    waterSurfaceData = float3(localPosition.xz, 0.0f);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

#endif

#endif
