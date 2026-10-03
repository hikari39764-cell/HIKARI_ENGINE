#ifndef HIKARI_WATER_SURFACE_DEFORM_INCLUDED
#define HIKARI_WATER_SURFACE_DEFORM_INCLUDED

#ifndef HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM
#define HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM 1
#endif

#if HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM

#include "Include/Meshlet/HIKARI_OceanGeometry.hlsli"
#include "Include/HIKARI_WaterSurfaceParams.hlsli"
#include "Include/Water/HIKARI_OceanSpectrumSampling.hlsli"

void HikariApplyWaterSurfaceDeformWithWorld(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    float4x4 deformationWorld,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    waterSurfaceData = float3(localPosition.xz, 0.0f);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_SURFACE) == 0u)
    {
        return;
    }

    const HikariWaterSurfaceParams loadedParams =
        HikariLoadWaterSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);
    HikariWaterSurfaceParams p = loadedParams;
    const float3 sourceLocalPosition = localPosition;
    const float3 authoredPosition = sourceLocalPosition;
    float3 oceanBaseLocalPosition = authoredPosition;
    float3 phasePosition = authoredPosition;
    float2 worldAnchorXZ = sourceLocalPosition.xz;

    if (p.cameraCenteredOcean)
    {
        const float2 authoredHalfExtent =
            HikariOceanResolveAuthoredHalfExtent(
                p.authoredHalfExtentXZ,
                instance.boundsCenterRadius.w,
                deformationWorld);
        const float2 safeAuthoredHalfExtent =
            HikariOceanSafeHalfExtent(authoredHalfExtent, 1.0f.xx);
        const float2 snapCellSize = HikariOceanResolveSnapCell(
            safeAuthoredHalfExtent,
            instance.clusterIndexCount);
        const float2 gridCenterWorldXZ =
            HikariOceanResolveGridCenterWorldXZ(
                deformationWorld,
                gInvViewProj,
                gCameraPos);
        const float2 gridCenterLocalXZ = HikariOceanWorldToLocalXZ(
            deformationWorld,
            gridCenterWorldXZ);
        const float2 snappedGridCenterLocalXZ = HikariOceanSnapCameraXZ(
            gridCenterLocalXZ,
            snapCellSize);
        const float2 centeredUv = HikariOceanResolveAuthoredCenteredXZ(
            authoredPosition.xz,
            uv1,
            p.authoredCenterXZ,
            safeAuthoredHalfExtent);
        // CameraCenteredOcean is a stable translated authored lattice.  The
        // Scene render-data bridge has already applied the one authoritative
        // wavelength-to-grid sampling filter.  Stretching this same lattice
        // toward the far plane and filtering it again can reduce every valid
        // carrier to zero, producing a perfectly flat ocean while the finite
        // route still shows the same waves.
        localPosition.xz = snappedGridCenterLocalXZ +
            safeAuthoredHalfExtent * centeredUv;
        oceanBaseLocalPosition = localPosition;
        phasePosition = localPosition;
        worldAnchorXZ = HikariOceanWorldAnchoredXZ(
            deformationWorld,
            phasePosition);

        // HikariEvaluateWaterSurface uses sourceLocalPosition for both phase
        // and output.  Feed it the final world anchor for stable phase, then
        // transform only the resulting horizontal displacement back to local.
        phasePosition.xz = worldAnchorXZ;
    }

    const float2 spectrumWorldXZ = p.cameraCenteredOcean
        ? worldAnchorXZ
        : HikariOceanWorldAnchoredXZ(
            deformationWorld,
            sourceLocalPosition);
    HikariOceanSpectrumSample oceanSpectrum =
        (HikariOceanSpectrumSample)0;
    if (HikariWaterUsesOceanSpectrum(p))
    {
        oceanSpectrum = HikariSampleOceanSpectrum(
            spectrumWorldXZ,
            p.windDirectionRadians,
            p.shortWaveStrength,
            p.capillaryStrength);
    }
    // Authored carriers remain the macro-geometry authority and match the CPU
    // water query.  The optional spectrum is a bounded residual/detail field;
    // replacing all carriers with it made realistic water visually flat and
    // caused render/query disagreement.
    const float spectrumResidualWeight = oceanSpectrum.connected != 0u
        ? HIKARI_WATER_SPECTRUM_RESIDUAL_WEIGHT
        : 0.0f;

    const HikariWaterSurfaceEvaluation evaluation =
        HikariEvaluateWaterSurface(
            p,
            phasePosition,
            simulationTime);
    if (p.cameraCenteredOcean)
    {
        float2 worldDisplacement =
            evaluation.positionLocal.xz - phasePosition.xz;
        float verticalDisplacement =
            evaluation.positionLocal.y - phasePosition.y;
        if (oceanSpectrum.connected != 0u)
        {
            const float horizontalScale =
                HikariWaterSpectrumHorizontalScale(loadedParams.choppiness);
            worldDisplacement += oceanSpectrum.displacement.xz *
                horizontalScale * spectrumResidualWeight;
            verticalDisplacement += oceanSpectrum.displacement.y *
                spectrumResidualWeight;
        }
        localPosition.xz = oceanBaseLocalPosition.xz +
            HikariOceanWorldDeltaToLocalXZ(
                deformationWorld,
                worldDisplacement);
        localPosition.y = oceanBaseLocalPosition.y +
            verticalDisplacement;
    }
    else
    {
        localPosition = evaluation.positionLocal;
        if (oceanSpectrum.connected != 0u)
        {
            const float horizontalScale =
                HikariWaterSpectrumHorizontalScale(loadedParams.choppiness);
            localPosition.xz += HikariOceanWorldDeltaToLocalXZ(
                deformationWorld,
                oceanSpectrum.displacement.xz * horizontalScale *
                    spectrumResidualWeight);
            localPosition.y += oceanSpectrum.displacement.y *
                spectrumResidualWeight;
        }
    }
    if (oceanSpectrum.connected != 0u)
    {
        if (p.cameraCenteredOcean)
        {
            const float3 analyticNormalWS = normalize(
                evaluation.normalLocal);
            const float analyticNormalY = max(
                analyticNormalWS.y,
                0.10f);
            const float2 analyticSlope =
                -analyticNormalWS.xz / analyticNormalY;
            const float2 combinedSlope = analyticSlope +
                oceanSpectrum.macroSlope * spectrumResidualWeight;
            const float3 combinedNormalWS = normalize(float3(
                -combinedSlope.x,
                1.0f,
                -combinedSlope.y));
            localNormal = HikariOceanWorldNormalToLocal(
                deformationWorld,
                combinedNormalWS);
        }
        else
        {
            const float3 spectrumNormalLocal =
                HikariOceanWorldNormalToLocal(
                    deformationWorld,
                    normalize(float3(
                        -oceanSpectrum.macroSlope.x,
                        1.0f,
                        -oceanSpectrum.macroSlope.y)));
            const float3 localUp = HikariOceanWorldNormalToLocal(
                deformationWorld,
                float3(0.0f, 1.0f, 0.0f));
            localNormal = normalize(
                evaluation.normalLocal +
                (spectrumNormalLocal - localUp) *
                    spectrumResidualWeight);
        }
    }
    else
    {
        localNormal = p.cameraCenteredOcean
            ? HikariOceanWorldNormalToLocal(
                deformationWorld,
                evaluation.normalLocal)
            : evaluation.normalLocal;
    }
    const float2 surfaceDomain = p.cameraCenteredOcean
        ? worldAnchorXZ
        : sourceLocalPosition.xz;
    waterSurfaceData = float3(
        surfaceDomain,
        oceanSpectrum.connected != 0u
            ? max(
                evaluation.compression,
                saturate(1.0f - oceanSpectrum.jacobian) *
                    spectrumResidualWeight)
            : evaluation.compression);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    HikariApplyWaterSurfaceDeformWithWorld(
        instance,
        simulationTime,
        uv1,
        instance.world,
        localPosition,
        localNormal,
        waterSurfaceData);
}

void HikariApplyWaterSurfaceDeformWithWorld(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    float4x4 deformationWorld,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeformWithWorld(
        instance,
        simulationTime,
        uv1,
        deformationWorld,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        uv1,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

// Legacy callers retain the finite route exactly.  Production water callers
// pass the authored UV1 explicitly so CameraCenteredOcean cannot collapse to
// an inferred or camera-heuristic mode.
void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        float2(0.5f, 0.5f),
        localPosition,
        localNormal,
        waterSurfaceData);
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
        float2(0.5f, 0.5f),
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

#else

void HikariApplyWaterSurfaceDeformWithWorld(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    float4x4 deformationWorld,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    (void)instance;
    (void)simulationTime;
    (void)uv1;
    (void)deformationWorld;
    (void)localNormal;
    waterSurfaceData = float3(localPosition.xz, 0.0f);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    HikariApplyWaterSurfaceDeformWithWorld(
        instance,
        simulationTime,
        uv1,
        instance.world,
        localPosition,
        localNormal,
        waterSurfaceData);
}

void HikariApplyWaterSurfaceDeformWithWorld(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    float4x4 deformationWorld,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeformWithWorld(
        instance,
        simulationTime,
        uv1,
        deformationWorld,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    float2 uv1,
    inout float3 localPosition,
    inout float3 localNormal)
{
    float3 unusedWaterSurfaceData;
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        uv1,
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

void HikariApplyWaterSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    float simulationTime,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 waterSurfaceData)
{
    HikariApplyWaterSurfaceDeform(
        instance,
        simulationTime,
        float2(0.5f, 0.5f),
        localPosition,
        localNormal,
        waterSurfaceData);
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
        float2(0.5f, 0.5f),
        localPosition,
        localNormal,
        unusedWaterSurfaceData);
}

#endif

#endif
