#ifndef HIKARI_WATER_SURFACE_PARAMS_INCLUDED
#define HIKARI_WATER_SURFACE_PARAMS_INCLUDED

#include "Include/Math/HIKARI_FiniteMath.hlsli"

static const uint HIKARI_WATER_SURFACE_GPU_SCHEMA_VERSION = 1u;
static const uint HIKARI_WATER_SURFACE_GPU_FLAG_ENABLED = 1u << 0;
static const uint HIKARI_WATER_SURFACE_WAVE_COUNT = 4u;
static const float HIKARI_WATER_TIME_WRAP_SECONDS = 4096.0f;
static const float HIKARI_WATER_SHADING_ANIME_SUMMER = 0.0f;
static const float HIKARI_WATER_SHADING_REALISTIC_CLEAR_SUMMER = 1.0f;

// C++ 側の WaterSurfaceWaveGpuData / WaterSurfaceGpuData と同一 layout。
// 波向き、波数、角周波数は scene snapshot から CPU で一度だけ計算する。
struct HikariWaterSurfaceWaveGpuData
{
    // (direction.x, direction.y, amplitude, waveNumber)
    float4 directionAmplitudeWaveNumber;
    // (angularFrequency, steepness, reserved, reserved)
    float4 angularFrequencySteepness;
};

struct HikariWaterSurfaceGpuData
{
    // (schemaVersion, flags, waveCount, recordCount)
    // recordCount は index 0 sentinel のみが保持する。
    uint4 header;
    // (timeScale, choppiness, shadingModel, microWaveStrength)
    float4 surfaceParams;
    // (depthTransmittance.rgb, foamAmount)
    float4 depthTransmittanceFoam;
    // (scatteringColor.rgb, absorptionDistance)
    float4 scatteringAbsorption;
    // (foamColor.rgb, surfaceRoughness)
    float4 foamRoughness;
    // (coverageResolution, reserved, reserved, reserved)
    float4 coverageParams;
    HikariWaterSurfaceWaveGpuData waves[HIKARI_WATER_SURFACE_WAVE_COUNT];
};

// ROOT_PARAM::SurfaceFeatureData (t0, space7)。index 0 は常に無効な中立 sentinel。
StructuredBuffer<HikariWaterSurfaceGpuData> gWaterSurfaceData : register(t0, space7);

struct HikariWaterWaveParams
{
    float2 direction;
    float amplitude;
    float waveNumber;
    float angularFrequency;
    float steepness;
};

struct HikariWaterSurfaceParams
{
    float timeScale;
    float choppiness;
    float shadingModel;
    float microWaveStrength;
    float3 depthTransmittance;
    float foamAmount;
    float3 scatteringColor;
    float absorptionDistance;
    float3 foamColor;
    float surfaceRoughness;
    uint waveCount;
    uint liquidCoverageOffset;
    uint coverageResolution;
    HikariWaterWaveParams waves[HIKARI_WATER_SURFACE_WAVE_COUNT];
};

struct HikariWaterSurfaceEvaluation
{
    float3 positionLocal;
    float3 normalLocal;
    float3 velocityLocal;
    float compression;
    float horizontalJacobian;
};

HikariWaterSurfaceParams HikariBuildNeutralWaterSurfaceParams()
{
    HikariWaterSurfaceParams p = (HikariWaterSurfaceParams)0;
    p.depthTransmittance = 1.0f.xxx;
    p.foamColor = 1.0f.xxx;
    p.absorptionDistance = 1.0f;
    p.surfaceRoughness = 1.0f;
    p.coverageResolution = 1u;
    return p;
}

HikariWaterSurfaceParams HikariLoadWaterSurfaceParamsFromDataIndex(
    uint surfaceFeatureDataIndex)
{
    HikariWaterSurfaceParams p = HikariBuildNeutralWaterSurfaceParams();
    // Root SRV descriptor は GPU VA だけを持ち、buffer size を持たない。
    // GetDimensions へ依存せず、必ず存在する index 0 sentinel の envelope で
    // binding と bounds を検証してから feature record を読む。
    const HikariWaterSurfaceGpuData sentinel = gWaterSurfaceData[0];
    const bool validEnvelope =
        sentinel.header.x == HIKARI_WATER_SURFACE_GPU_SCHEMA_VERSION &&
        sentinel.header.y == 0u &&
        sentinel.header.z == 0u &&
        sentinel.header.w > 0u;
    if (!validEnvelope ||
        surfaceFeatureDataIndex == 0u ||
        surfaceFeatureDataIndex >= sentinel.header.w)
    {
        return p;
    }
    const HikariWaterSurfaceGpuData data =
        gWaterSurfaceData[surfaceFeatureDataIndex];
    if (data.header.x != HIKARI_WATER_SURFACE_GPU_SCHEMA_VERSION ||
        (data.header.y & HIKARI_WATER_SURFACE_GPU_FLAG_ENABLED) == 0u)
    {
        return p;
    }

    p.timeScale = HikariFiniteOr(data.surfaceParams.x, 1.0f);
    p.choppiness = max(HikariFiniteOr(data.surfaceParams.y, 0.0f), 0.0f);
    p.shadingModel =
        HikariFiniteOr(data.surfaceParams.z, 0.0f) >= 0.5f
            ? HIKARI_WATER_SHADING_REALISTIC_CLEAR_SUMMER
            : HIKARI_WATER_SHADING_ANIME_SUMMER;
    p.microWaveStrength = max(
        HikariFiniteOr(data.surfaceParams.w, 0.0f),
        0.0f);
    p.depthTransmittance = saturate(HikariFiniteOr(
        data.depthTransmittanceFoam.rgb,
        1.0f.xxx));
    p.foamAmount = saturate(HikariFiniteOr(
        data.depthTransmittanceFoam.w,
        0.0f));
    p.scatteringColor = clamp(
        HikariFiniteOr(data.scatteringAbsorption.rgb, 0.0f.xxx),
        0.0f.xxx,
        16.0f.xxx);
    p.absorptionDistance = max(
        HikariFiniteOr(data.scatteringAbsorption.w, 1.0f),
        0.001f);
    p.foamColor = clamp(
        HikariFiniteOr(data.foamRoughness.rgb, 1.0f.xxx),
        0.0f.xxx,
        16.0f.xxx);
    p.surfaceRoughness = clamp(
        HikariFiniteOr(data.foamRoughness.w, 1.0f),
        0.02f,
        1.0f);
    p.waveCount = min(data.header.z, HIKARI_WATER_SURFACE_WAVE_COUNT);
    p.liquidCoverageOffset = data.header.w;
    p.coverageResolution = max((uint)data.coverageParams.x, 1u);
    [unroll]
    for (uint index = 0u; index < HIKARI_WATER_SURFACE_WAVE_COUNT; ++index)
    {
        const HikariWaterSurfaceWaveGpuData wave = data.waves[index];
        float2 direction = HikariFiniteOr(
            wave.directionAmplitudeWaveNumber.xy,
            float2(1.0f, 0.0f));
        const float directionLengthSquared = dot(direction, direction);
        direction = HikariIsFiniteFloat(directionLengthSquared) &&
                directionLengthSquared > 1.0e-12f
            ? direction * rsqrt(directionLengthSquared)
            : float2(1.0f, 0.0f);
        p.waves[index].direction = direction;
        p.waves[index].amplitude = max(HikariFiniteOr(
            wave.directionAmplitudeWaveNumber.z,
            0.0f), 0.0f);
        p.waves[index].waveNumber = max(HikariFiniteOr(
            wave.directionAmplitudeWaveNumber.w,
            0.0f), 0.0f);
        p.waves[index].angularFrequency = max(HikariFiniteOr(
            wave.angularFrequencySteepness.x,
            0.0f), 0.0f);
        p.waves[index].steepness = max(HikariFiniteOr(
            wave.angularFrequencySteepness.y,
            0.0f), 0.0f);
    }
    return p;
}

HikariWaterSurfaceParams HikariLoadWaterSurfaceParams(
    uint surfaceGpuSceneIndex)
{
    const HikariSurfaceGpuSceneInstance instance =
        gSurfaceGpuSceneBuffer[surfaceGpuSceneIndex];
    return HikariLoadWaterSurfaceParamsFromDataIndex(
        instance.surfaceFeatureDataIndex);
}

float HikariWrapWaterSimulationTime(float simulationTime, float timeScale)
{
    const float safeSimulationTime = HikariFiniteOr(simulationTime, 0.0f);
    const float safeTimeScale = HikariFiniteOr(timeScale, 1.0f);
    return fmod(
        safeSimulationTime,
        HIKARI_WATER_TIME_WRAP_SECONDS) * safeTimeScale;
}

HikariWaterSurfaceEvaluation HikariEvaluateWaterSurface(
    HikariWaterSurfaceParams p,
    float3 sourceLocalPosition,
    float simulationTime)
{
    HikariWaterSurfaceEvaluation result = (HikariWaterSurfaceEvaluation)0;
    result.positionLocal = sourceLocalPosition;
    float3 tangentX = float3(1.0f, 0.0f, 0.0f);
    float3 tangentZ = float3(0.0f, 0.0f, 1.0f);
    const float animationTime =
        HikariWrapWaterSimulationTime(simulationTime, p.timeScale);
    const float inverseWaveCount = rcp((float)max(p.waveCount, 1u));

    [unroll]
    for (uint index = 0u; index < HIKARI_WATER_SURFACE_WAVE_COUNT; ++index)
    {
        if (index >= p.waveCount)
        {
            continue;
        }
        const HikariWaterWaveParams wave = p.waves[index];
        if (wave.amplitude <= 1.0e-7f || wave.waveNumber <= 1.0e-7f)
        {
            continue;
        }
        const float phase =
            wave.waveNumber * dot(wave.direction, sourceLocalPosition.xz) -
            wave.angularFrequency * animationTime;
        float sine;
        float cosine;
        sincos(phase, sine, cosine);
        const float horizontalAmplitude =
            p.choppiness * wave.steepness * inverseWaveCount /
            wave.waveNumber;

        result.positionLocal.xz +=
            wave.direction * horizontalAmplitude * cosine;
        result.positionLocal.y += wave.amplitude * sine;

        const float horizontalDerivative =
            horizontalAmplitude * wave.waveNumber * sine;
        const float verticalDerivative =
            wave.amplitude * wave.waveNumber * cosine;
        tangentX += float3(
            -wave.direction.x * wave.direction.x * horizontalDerivative,
            wave.direction.x * verticalDerivative,
            -wave.direction.x * wave.direction.y * horizontalDerivative);
        tangentZ += float3(
            -wave.direction.x * wave.direction.y * horizontalDerivative,
            wave.direction.y * verticalDerivative,
            -wave.direction.y * wave.direction.y * horizontalDerivative);

        const float timeDerivative = wave.angularFrequency * p.timeScale;
        result.velocityLocal.xz +=
            wave.direction * horizontalAmplitude * timeDerivative * sine;
        result.velocityLocal.y -=
            wave.amplitude * timeDerivative * cosine;
    }

    const float3 unnormalizedNormal = cross(tangentZ, tangentX);
    const float normalLengthSquared = dot(
        unnormalizedNormal,
        unnormalizedNormal);
    result.normalLocal = normalLengthSquared > 1.0e-12f
        ? unnormalizedNormal * rsqrt(normalLengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
    result.horizontalJacobian =
        tangentX.x * tangentZ.z - tangentX.z * tangentZ.x;
    result.compression = saturate(1.0f - result.horizontalJacobian);
    return result;
}

#endif
