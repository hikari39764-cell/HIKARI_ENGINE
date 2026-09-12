#ifndef HIKARI_LOCAL_LIGHTING_PROVIDER_COMMON_HLSLI
#define HIKARI_LOCAL_LIGHTING_PROVIDER_COMMON_HLSLI

#include "../Lighting/HIKARI_IrradianceSh9.hlsli"

float3 HikariSampleLightProbeCoefficient(
    uint providerSlot,
    uint coefficientIndex,
    float3 uvw)
{
    return providerSlot == 0u
        ? gLightProbeShVolume0[coefficientIndex].SampleLevel(
            gLinearWrap,
            uvw,
            0.0f).rgb
        : gLightProbeShVolume1[coefficientIndex].SampleLevel(
            gLinearWrap,
            uvw,
            0.0f).rgb;
}

// SH9 係数を Texture3D 上で補間してから拡散照明を評価する。
float3 HikariEvaluateLightProbeVolumeDiffuse(
    uint providerSlot,
    HikariIrradianceVolumeData volume,
    float3 worldPos,
    float3 n)
{
    uint3 counts = (uint3)(
        volume.countsProbeCount.xyz + 0.5f);
    uint probeCount = (uint)(
        volume.countsProbeCount.w + 0.5f);
    if (volume.originSampling.w < 0.5f ||
        probeCount == 0u ||
        any(counts < 1u))
    {
        return 0.0f.xxx;
    }

    float3 floatCounts = (float3)counts;
    float spacingMinimum = min(
        min(
            volume.spacingIntensity.x,
            volume.spacingIntensity.y),
        volume.spacingIntensity.z);
    float normalBiasDistance =
        spacingMinimum *
        clamp(volume.selectionParams.z, 0.0f, 0.5f);
    float3 sampleWorldPosition =
        worldPos + normalize(n) * normalBiasDistance;
    float3 gridCoord =
        (sampleWorldPosition - volume.originSampling.xyz) /
        max(
            volume.spacingIntensity.xyz,
            0.0001f.xxx);
    gridCoord = clamp(
        gridCoord,
        0.0f.xxx,
        floatCounts - 1.0f);
    float3 uvw =
        (gridCoord + 0.5f) / floatCounts;

    float3 result = 0.0f.xxx;
    [unroll]
    for (uint coefficient = 0u;
        coefficient < 9u;
        ++coefficient)
    {
        result += HikariSampleLightProbeCoefficient(
            providerSlot,
            coefficient,
            uvw) *
            HikariSh9DiffuseWeight(n, coefficient);
    }
    return max(result, 0.0f.xxx);
}

float HikariSmoothLightingProviderWeight(float value)
{
    value = saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

float HikariEvaluateIrradianceVolumeInfluence(
    HikariIrradianceVolumeData volume,
    float3 worldPos)
{
    if (volume.selectionParams.y < 0.5f ||
        volume.originSampling.w < 0.5f ||
        volume.spacingIntensity.w <= 0.0001f ||
        volume.countsProbeCount.w < 0.5f ||
        any(volume.countsProbeCount.xyz < 0.5f.xxx))
    {
        return 0.0f;
    }

    float3 boxMin = volume.originSampling.xyz;
    float3 boxMax =
        boxMin + max(
            volume.sizeBlendDistance.xyz,
            0.001f.xxx);
    if (any(worldPos < boxMin) ||
        any(worldPos > boxMax))
    {
        return 0.0f;
    }

    float3 toMin = worldPos - boxMin;
    float3 toMax = boxMax - worldPos;
    float edgeDistance = min(
        min(min(toMin.x, toMin.y), toMin.z),
        min(min(toMax.x, toMax.y), toMax.z));
    float halfMinSize = max(
        0.001f,
        min(
            min(
                volume.sizeBlendDistance.x,
                volume.sizeBlendDistance.y),
            volume.sizeBlendDistance.z) *
            0.5f);
    float blendDistance = min(
        max(0.001f, volume.sizeBlendDistance.w),
        halfMinSize);
    return HikariSmoothLightingProviderWeight(
        edgeDistance / blendDistance);
}

float2 HikariEvaluateIrradianceProviderWeights(
    float3 worldPos)
{
    float2 weights;
    weights.x = HikariEvaluateIrradianceVolumeInfluence(
        gIrradianceVolumes[0],
        worldPos);
    weights.y = HikariEvaluateIrradianceVolumeInfluence(
        gIrradianceVolumes[1],
        worldPos);
    return saturate(weights);
}

float HikariEvaluateProviderFallbackWeight(float2 weights)
{
    return saturate(
        (1.0f - weights.x) *
        (1.0f - weights.y));
}

float3 HikariBlendLightProbeDiffuse(
    float3 fallbackDiffuse,
    float3 worldPos,
    float3 n)
{
    float weights[2];
    float3 samples[2];
    [unroll]
    for (uint slot = 0u; slot < 2u; ++slot)
    {
        HikariIrradianceVolumeData volume =
            gIrradianceVolumes[slot];
        weights[slot] =
            HikariEvaluateIrradianceVolumeInfluence(
                volume,
                worldPos);
        samples[slot] = weights[slot] > 0.0001f
            ? HikariEvaluateLightProbeVolumeDiffuse(
                slot,
                volume,
                worldPos,
                n) *
                max(0.0f, volume.spacingIntensity.w)
            : 0.0f.xxx;
    }

    if (weights[0] <= 0.0001f &&
        weights[1] <= 0.0001f)
    {
        return fallbackDiffuse;
    }

    float priority0 =
        gIrradianceVolumes[0].selectionParams.x;
    float priority1 =
        gIrradianceVolumes[1].selectionParams.x;
    if (priority0 > priority1)
    {
        float3 lower = lerp(
            fallbackDiffuse,
            samples[1],
            weights[1]);
        return lerp(
            lower,
            samples[0],
            weights[0]);
    }
    if (priority1 > priority0)
    {
        float3 lower = lerp(
            fallbackDiffuse,
            samples[0],
            weights[0]);
        return lerp(
            lower,
            samples[1],
            weights[1]);
    }

    float totalWeight =
        weights[0] + weights[1];
    float3 localDiffuse =
        (samples[0] * weights[0] +
            samples[1] * weights[1]) /
        max(totalWeight, 0.0001f);
    float localCoverage =
        1.0f -
        (1.0f - weights[0]) *
        (1.0f - weights[1]);
    return lerp(
        fallbackDiffuse,
        localDiffuse,
        saturate(localCoverage));
}

float HikariEvaluateSphereReflectionProbeInfluence(
    HikariReflectionProbeData probe,
    float3 worldPos)
{
    float radius =
        max(0.001f, probe.positionRadius.w);
    float blendDistance = min(
        max(0.001f, probe.shapeParams.z),
        radius);
    float distanceToProbe = length(
        worldPos - probe.positionRadius.xyz);
    return HikariSmoothLightingProviderWeight(
        (radius - distanceToProbe) /
        blendDistance);
}

float HikariEvaluateBoxReflectionProbeInfluence(
    HikariReflectionProbeData probe,
    float3 worldPos)
{
    if (any(worldPos < probe.influenceBoxMin.xyz) ||
        any(worldPos > probe.influenceBoxMax.xyz))
    {
        return 0.0f;
    }

    float3 toMin =
        worldPos - probe.influenceBoxMin.xyz;
    float3 toMax =
        probe.influenceBoxMax.xyz - worldPos;
    float edgeDistance = min(
        min(min(toMin.x, toMin.y), toMin.z),
        min(min(toMax.x, toMax.y), toMax.z));
    float3 boxSize = max(
        probe.influenceBoxMax.xyz -
            probe.influenceBoxMin.xyz,
        0.001f.xxx);
    float halfMinSize = max(
        0.001f,
        min(min(boxSize.x, boxSize.y), boxSize.z) *
            0.5f);
    float blendDistance = min(
        max(0.001f, probe.shapeParams.z),
        halfMinSize);
    return HikariSmoothLightingProviderWeight(
        edgeDistance / blendDistance);
}

float HikariEvaluateReflectionProbeInfluence(
    HikariReflectionProbeData probe,
    float3 worldPos)
{
    float influence = probe.shapeParams.x > 0.5f
        ? HikariEvaluateBoxReflectionProbeInfluence(
            probe,
            worldPos)
        : HikariEvaluateSphereReflectionProbeInfluence(
            probe,
            worldPos);
    return saturate(influence);
}

float2 HikariEvaluateReflectionProviderWeights(
    float3 worldPos)
{
    float2 weights = 0.0f.xx;
    [unroll]
    for (uint slot = 0u; slot < 2u; ++slot)
    {
        HikariReflectionProbeData probe =
            gReflectionProbes[slot];
        if (probe.params.x > 0.5f &&
            probe.params.y > 0.5f)
        {
            weights[slot] =
                HikariEvaluateReflectionProbeInfluence(
                    probe,
                    worldPos);
        }
    }
    return saturate(weights);
}

float3 HikariSafeProbeDirection(float3 direction)
{
    float lengthSquared =
        dot(direction, direction);
    return lengthSquared > 1e-8f
        ? direction * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

float3 HikariSafeProbeRayDirection(float3 direction)
{
    return float3(
        abs(direction.x) > 1e-5f
            ? direction.x
            : (direction.x < 0.0f
                ? -1e-5f
                : 1e-5f),
        abs(direction.y) > 1e-5f
            ? direction.y
            : (direction.y < 0.0f
                ? -1e-5f
                : 1e-5f),
        abs(direction.z) > 1e-5f
            ? direction.z
            : (direction.z < 0.0f
                ? -1e-5f
                : 1e-5f));
}

bool HikariRayIntersectsProbeBox(
    float3 origin,
    float3 direction,
    float3 boxMin,
    float3 boxMax,
    out float hitT)
{
    float3 safeDirection =
        HikariSafeProbeRayDirection(direction);
    float3 t0 =
        (boxMin - origin) / safeDirection;
    float3 t1 =
        (boxMax - origin) / safeDirection;
    float3 tNear3 = min(t0, t1);
    float3 tFar3 = max(t0, t1);
    float tNear =
        max(max(tNear3.x, tNear3.y), tNear3.z);
    float tFar =
        min(min(tFar3.x, tFar3.y), tFar3.z);
    bool inside =
        all(origin >= boxMin) &&
        all(origin <= boxMax);
    hitT = inside ? tFar : tNear;
    return tFar >= max(tNear, 0.0f) &&
        hitT >= 0.0f;
}

float3 HikariComputeReflectionProbeSampleDirection(
    HikariReflectionProbeData probe,
    float3 worldPos,
    float3 reflectionDirection)
{
    if (probe.shapeParams.y < 0.5f)
    {
        return reflectionDirection;
    }

    float3 projectionSize =
        probe.projectionBoxMax.xyz -
        probe.projectionBoxMin.xyz;
    if (any(projectionSize <= 0.001f.xxx))
    {
        return reflectionDirection;
    }

    float hitT = 0.0f;
    if (!HikariRayIntersectsProbeBox(
        worldPos,
        reflectionDirection,
        probe.projectionBoxMin.xyz,
        probe.projectionBoxMax.xyz,
        hitT))
    {
        return reflectionDirection;
    }

    float3 hitPosition =
        worldPos + reflectionDirection * hitT;
    return HikariSafeProbeDirection(
        hitPosition - probe.positionRadius.xyz);
}

#endif
