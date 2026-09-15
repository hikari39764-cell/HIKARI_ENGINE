#ifndef HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI
#define HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI

#include "HIKARI_IrradianceSh9.hlsli"
#include "../HIKARI_DirectionEncoding.hlsli"

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

// 可視性は probe から受光点への方向距離。位置補間は元の座標を維持する。
float HikariLightProbeVisibility(HikariIrradianceVolumeData volume,
    uint3 probeCoordinate, float3 worldPos, float requestedBias, float3 relocation = 0.0f.xxx)
{
    const uint descriptorIndex = (uint)(volume.selectionParams.y + 0.5f) - 1u;
    if (descriptorIndex >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT || volume.selectionParams.w <= 0.0f)
        return 0.0f;
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 probePosition = volume.originSampling.xyz + ((float3)probeCoordinate + relocation) * volume.spacingIntensity.xyz;
    const float3 offset = worldPos - probePosition;
    const float distance = length(offset);
    if (distance <= 1e-6f) return 1.0f;
    const float2 directionUv = HikariEncodeOctahedralDirection(offset / distance);
    const uint2 tile = uint2(probeCoordinate.x + probeCoordinate.y * counts.x, probeCoordinate.z);
    const float side = volume.originSampling.w >= 2.5f ? 8.0f : 16.0f;
    const float2 atlasSize = float2(counts.x * counts.y, counts.z) * (side+2.0f);
    const float2 uv = ((float2)tile * (side+2.0f) + 1.0f + directionUv * side) / atlasSize;
    const float2 moments = gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleLevel(gLinearClamp, uv, 0.0f).rg;
    // 大きな probe 間隔でも壁を越す位置オフセットにしない。距離比較の誤差だけ許容する。
    const float bias = min(requestedBias, max(0.001f, distance * 0.001f));
    const float delta = max(0.0f, (distance - bias) / volume.selectionParams.w - moments.x);
    const float variance = max(0.0f, moments.y - moments.x * moments.x);
    const float visibility = delta <= 0.0f ? 1.0f : variance / max(variance + delta * delta, 1e-20f);
    return visibility * visibility * visibility;
}

float4 HikariLoadLightProbeTexel(uint providerSlot, uint coefficientIndex, uint3 coordinate)
{
    return providerSlot == 0u
        ? gLightProbeShVolume0[coefficientIndex].Load(int4(coordinate, 0))
        : gLightProbeShVolume1[coefficientIndex].Load(int4(coordinate, 0));
}

float3 HikariLoadLightProbeCoefficient(uint providerSlot, uint coefficientIndex, uint3 coordinate)
{
    return HikariLoadLightProbeTexel(providerSlot, coefficientIndex, coordinate).rgb;
}

// データ不在と、正しく暗い（遮蔽された）照明を別に扱う。
bool HikariHasIrradianceVolumeData(HikariIrradianceVolumeData volume)
{
    return volume.originSampling.w >= 0.5f && volume.selectionParams.y >= 0.5f &&
        volume.selectionParams.y <= (float)HIKARI_MATERIAL_TEXTURE_POOL_COUNT &&
        volume.selectionParams.w > 0.0f && isfinite(volume.selectionParams.w) &&
        volume.countsProbeCount.w >= 0.5f && all(volume.countsProbeCount.xyz >= 1.0f.xxx) &&
        all(isfinite(volume.countsProbeCount)) && all(isfinite(volume.spacingIntensity)) &&
        all(volume.spacingIntensity.xyz > 0.0f.xxx) && volume.spacingIntensity.w >= 0.0f;
}

// RGB は E/pi、A は sample validity。空間 coverage は influence が所有する。
float4 HikariEvaluateLightProbeVolumeDiffuse(
    uint providerSlot, HikariIrradianceVolumeData volume, float3 worldPos, float3 n)
{
    if (!HikariHasIrradianceVolumeData(volume)) return 0.0f.xxxx;
    // 強度ゼロは有効な黒。下位の空へ切り替えると調整結果が逆転する。
    if (volume.spacingIntensity.w == 0.0f) return float4(0,0,0,1);
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 grid = clamp((worldPos - volume.originSampling.xyz) /
        max(volume.spacingIntensity.xyz, 0.0001f.xxx), 0.0f.xxx, (float3)(counts - 1u));
    const uint3 base = (uint3)floor(grid);
    const float3 fraction = frac(grid);
    const float minimumSpacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
    const float requestedBias = minimumSpacing * clamp(volume.selectionParams.z, 0.0f, 0.5f);
    uint3 coordinates[8];
    float weights[8];
    float totalWeight = 0.0f;
    float validWeight = 0.0f;
    const bool dynamic = volume.originSampling.w >= 2.5f;
    bool allVisible = !dynamic;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        coordinates[corner] = min(base + offset, counts - 1u);
        const float3 axisWeight = lerp(1.0f.xxx - fraction, fraction, (float3)offset);
        const float interpolationWeight = axisWeight.x * axisWeight.y * axisWeight.z;
        const float validity = dynamic ? saturate(HikariLoadLightProbeTexel(providerSlot, 0u, coordinates[corner]).a) : 1.0f;
        float3 relocation = 0.0f.xxx;
        if (dynamic && interpolationWeight > 0.0f && validity > 0.0f)
            relocation = float3(HikariLoadLightProbeTexel(providerSlot, 1u, coordinates[corner]).a,
                HikariLoadLightProbeTexel(providerSlot, 2u, coordinates[corner]).a,
                HikariLoadLightProbeTexel(providerSlot, 3u, coordinates[corner]).a);
        const float visibility = interpolationWeight > 0.0f
            ? HikariLightProbeVisibility(volume, coordinates[corner], worldPos, requestedBias, relocation) : 1.0f;
        weights[corner] = interpolationWeight * visibility * validity;
        validWeight += interpolationWeight * validity;
        totalWeight += weights[corner];
        allVisible = allVisible && visibility >= 0.9999f;
    }
    // 全 probe が壁の反対側でもデータは有効。sky fallback による漏光を作らない。
    if (validWeight <= 1e-5f) return 0.0f.xxxx;
    if (totalWeight <= 1e-5f) return float4(0,0,0,saturate(validWeight));
    float3 result = 0.0f.xxx;
    [branch] if (allVisible)
    {
        const float3 uvw = (grid + 0.5f) / (float3)counts;
        [unroll] for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
            result += HikariSampleLightProbeCoefficient(providerSlot, coefficient, uvw) *
                HikariSh9DiffuseWeight(n, coefficient);
    }
    else if (!dynamic && volume.originSampling.w >= 1.5f)
    {
        // Fast: 可視性で重み付けした x 方向の二点を hardware filtering 一回へ畳む。
        // y/z は texel 中心に固定。遮蔽重みは省略せず、36 fetch で同じ SH 線形和を評価する。
        [unroll] for (uint pair = 0u; pair < 4u; ++pair)
        {
            const uint corner = pair * 2u;
            const float pairWeight = weights[corner] + weights[corner + 1u];
            [branch] if (pairWeight > 0.0f)
            {
                const float fractionX = weights[corner + 1u] / pairWeight;
                const float3 coordinate = lerp((float3)coordinates[corner],
                    (float3)coordinates[corner + 1u], fractionX);
                const float3 uvw = (coordinate + 0.5f) / (float3)counts;
                float3 irradiance = 0.0f.xxx;
                [unroll] for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
                    irradiance += HikariSampleLightProbeCoefficient(providerSlot, coefficient, uvw) *
                        HikariSh9DiffuseWeight(n, coefficient);
                result += irradiance * pairWeight;
            }
        }
        result /= totalWeight;
    }
    else
    {
        // Full/dynamic: probe 単位で畳み込み、visibility の分岐を係数ごとに反復しない。
        // SH は signed。probe ごとの clamp はせず、補間後にだけ非負にする。
        [unroll] for (uint corner = 0u; corner < 8u; ++corner)
        {
            [branch] if (weights[corner] > 0.0f)
            {
                float3 irradiance = 0.0f.xxx;
                [unroll] for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
                    irradiance += HikariLoadLightProbeCoefficient(providerSlot, coefficient, coordinates[corner]) *
                        HikariSh9DiffuseWeight(n, coefficient);
                result += irradiance * weights[corner];
            }
        }
        result /= totalWeight;
    }
    return float4(max(result, 0.0f.xxx), saturate(validWeight));
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
    if (!HikariHasIrradianceVolumeData(volume))
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

#if !defined(HIKARI_IRRADIANCE_SAMPLE_ONLY)
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

// RGB は coverage 適用済みの E/pi、A は下位 provider を置換する coverage。
// 呼び出し側は A<1 の場合にだけ Sky/Ambient を評価する。
float4 HikariResolveLightProbeDiffuse(float3 worldPos, float3 n)
{
    float weights[2] = {0.0f, 0.0f};
    float3 samples[2] = {0.0f.xxx, 0.0f.xxx};
    const float priority0 = gIrradianceVolumes[0].selectionParams.x;
    const float priority1 = gIrradianceVolumes[1].selectionParams.x;
    const bool dynamicOverride = gIrradianceVolumes[0].originSampling.w >= 2.5f &&
        gIrradianceVolumes[1].originSampling.w < 2.5f;
    const bool ordered = priority0 != priority1 || dynamicOverride;
    const uint firstSlot = priority1 > priority0 && !dynamicOverride ? 1u : 0u;
    [unroll] for (uint order = 0u; order < 2u; ++order)
    {
        const uint slot = firstSlot ^ order;
        const HikariIrradianceVolumeData volume = gIrradianceVolumes[slot];
        const float influence = HikariEvaluateIrradianceVolumeInfluence(volume, worldPos);
        [branch] if (influence > 0.0001f)
        {
            const float4 sample = HikariEvaluateLightProbeVolumeDiffuse(slot, volume, worldPos, n);
            weights[slot] = influence * sample.a;
            samples[slot] = sample.rgb * max(0.0f, volume.spacingIntensity.w);
        }
        // 優先 provider が全面を覆うなら、隠れた下位 provider の SH を読まない。
        [branch] if (order == 0u && ordered && weights[slot] >= 1.0f)
            return float4(samples[slot], 1.0f);
    }
    if (weights[0] <= 0.0001f && weights[1] <= 0.0001f) return 0.0f.xxxx;
    const float coverage = saturate(1.0f - (1.0f - weights[0]) * (1.0f - weights[1]));
    if (ordered)
    {
        const uint lower = firstSlot ^ 1u;
        return float4(samples[lower] * (weights[lower] * (1.0f - weights[firstSlot])) +
            samples[firstSlot] * weights[firstSlot], coverage);
    }
    const float3 localDiffuse = (samples[0] * weights[0] + samples[1] * weights[1]) /
        max(weights[0] + weights[1], 0.0001f);
    return float4(localDiffuse * coverage, coverage);
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

#endif // HIKARI_IRRADIANCE_SAMPLE_ONLY
#endif
