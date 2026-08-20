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
        const float2 cameraLocalXZ = HikariOceanWorldToLocalXZ(
            deformationWorld,
            gCameraPos.xz);
        const float2 snappedCameraLocalXZ = HikariOceanSnapCameraXZ(
            cameraLocalXZ,
            snapCellSize);
        const float2 farHalfExtent = HikariOceanResolveFarHalfExtent(
            deformationWorld,
            gInvViewProj,
            gCameraPos,
            gCameraPos.xz,
            safeAuthoredHalfExtent);
        const float2 extentRatio = clamp(
            HikariFiniteOr(
                farHalfExtent / safeAuthoredHalfExtent,
                HIKARI_OCEAN_FALLBACK_FAR_EXTENT_RATIO.xx),
            1.0f.xx,
            HIKARI_OCEAN_MAX_FAR_EXTENT_RATIO.xx);
        const float2 centeredUv = HikariOceanResolveAuthoredCenteredXZ(
            authoredPosition.xz,
            uv1,
            p.authoredCenterXZ,
            safeAuthoredHalfExtent);
        const float2 gradedUv = centeredUv +
            (extentRatio - 1.0f.xx) *
            centeredUv * centeredUv * centeredUv;
        localPosition.xz = snappedCameraLocalXZ +
            safeAuthoredHalfExtent * gradedUv;
        oceanBaseLocalPosition = localPosition;
        phasePosition = localPosition;
        worldAnchorXZ = HikariOceanWorldAnchoredXZ(
            deformationWorld,
            phasePosition);

        const float2 worldCell = HikariOceanLocalDeltaToWorldXZ(
            deformationWorld,
            snapCellSize);
        const float viewDistance = length(
            mul(deformationWorld, float4(phasePosition, 1.0f)).xyz -
            gCameraPos.xyz);
        const float tanHalfFovY = HikariOceanResolveTanHalfFovY(
            gInvViewProj,
            gCameraPos);
        const float projectedCellPixels = HikariOceanProjectedCellPixels(
            length(worldCell),
            viewDistance,
            tanHalfFovY,
            gScreenParams.y);
        const float2 normalizedOffset =
            (phasePosition.xz - snappedCameraLocalXZ) /
            max(farHalfExtent, HIKARI_OCEAN_EPSILON.xx);
        const float highFrequencyFade = HikariOceanHighFrequencyFade(
            projectedCellPixels,
            length(normalizedOffset));
        // Long-wave height remains intact.  The horizontal high-frequency
        // choppiness fades continuously by projected footprint and radius;
        // short-wave vertical height/steepness follows the same footprint
        // with a wavelength/index-weighted floor so stretched far cells do
        // not alias while the primary swell remains present.
        [unroll]
        for (uint waveIndex = 0u;
             waveIndex < HIKARI_WATER_SURFACE_WAVE_COUNT;
             ++waveIndex)
        {
            if (waveIndex >= p.waveCount)
            {
                continue;
            }
            const float wavelength =
                6.28318530718f /
                max(p.waves[waveIndex].waveNumber, 1.0e-4f);
            const float wavelengthShortness = waveIndex == 0u
                ? 0.0f
                : saturate((4.0f - wavelength) / 3.0f);
            const float indexShortness = waveIndex == 0u
                ? 0.0f
                : waveIndex == 1u
                    ? 0.35f
                    : waveIndex == 2u
                        ? 0.75f
                        : 1.0f;
            const float minAmplitudeFactor = waveIndex == 0u
                ? 1.0f
                : waveIndex == 1u
                    ? 0.55f
                    : waveIndex == 2u
                        ? 0.22f
                        : 0.10f;
            const float highFrequencyWeight = max(
                wavelengthShortness,
                indexShortness);
            const float amplitudeFade = lerp(
                1.0f,
                minAmplitudeFactor,
                (1.0f - highFrequencyFade) * highFrequencyWeight);
            p.waves[waveIndex].amplitude *= amplitudeFade;
            p.waves[waveIndex].steepness *= amplitudeFade;
        }
        p.choppiness *= highFrequencyFade;

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
    const bool useOceanSpectrum =
        p.shadingModel >= HIKARI_WATER_SHADING_REALISTIC_CLEAR_SUMMER -
            0.25f &&
        (p.shortWaveStrength > 1.0e-4f ||
            p.capillaryStrength > 1.0e-4f);
    HikariOceanSpectrumSample oceanSpectrum =
        (HikariOceanSpectrumSample)0;
    if (useOceanSpectrum)
    {
        oceanSpectrum = HikariSampleOceanSpectrum(
            spectrumWorldXZ,
            p.windDirectionRadians,
            p.shortWaveStrength,
            p.capillaryStrength);
    }
    if (oceanSpectrum.connected != 0u)
    {
        // The frequency-domain field is the optional high-frequency geometry
        // authority for RealisticClearSummer. Keeping the analytic carriers
        // active at the same time would double count the short-wave energy.
        p.waveCount = 0u;
        p.choppiness = 0.0f;
    }

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
            const float horizontalScale = clamp(
                0.45f + loadedParams.choppiness * 0.75f,
                0.25f,
                1.35f);
            worldDisplacement += oceanSpectrum.displacement.xz *
                horizontalScale;
            verticalDisplacement += oceanSpectrum.displacement.y;
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
            const float horizontalScale = clamp(
                0.45f + loadedParams.choppiness * 0.75f,
                0.25f,
                1.35f);
            localPosition.xz += HikariOceanWorldDeltaToLocalXZ(
                deformationWorld,
                oceanSpectrum.displacement.xz * horizontalScale);
            localPosition.y += oceanSpectrum.displacement.y;
        }
    }
    if (oceanSpectrum.connected != 0u)
    {
        const float3 spectrumNormalWS = normalize(float3(
            -oceanSpectrum.macroSlope.x,
            1.0f,
            -oceanSpectrum.macroSlope.y));
        localNormal = HikariOceanWorldNormalToLocal(
            deformationWorld,
            spectrumNormalWS);
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
            ? saturate(1.0f - oceanSpectrum.jacobian)
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
