#ifndef HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI
#define HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI

#include "HIKARI_DynamicGiSampling.hlsli"

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

// 焼き込み volume の可視性は probe から受光点への方向距離。atlas は probe ごとに 16x16 の八面体。
float HikariLightProbeVisibility(HikariIrradianceVolumeData volume, uint3 probeCoordinate, float3 worldPos)
{
    const uint descriptorIndex = (uint)(volume.selectionParams.y + 0.5f) - 1u;
    if (descriptorIndex >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT || volume.selectionParams.w <= 0.0f)
        return 0.0f;
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 offset = worldPos - (volume.originSampling.xyz + (float3)probeCoordinate * volume.spacingIntensity.xyz);
    const float distance = length(offset);
    if (distance <= 1e-6f) return 1.0f;
    const float side = 16.0f;
    const float2 tile = float2(probeCoordinate.x + probeCoordinate.y * counts.x, probeCoordinate.z) * (side + 2.0f);
    const float2 atlasSize = float2(counts.x * counts.y, counts.z) * (side + 2.0f);
    const float2 moments = gMaterialTexturePool[descriptorIndex].SampleLevel(gLinearClamp,
        HikariProbeAtlasUv(tile, side, HikariEncodeOctahedralDirection(offset / distance), atlasSize), 0.0f).rg;
    return HikariProbeChebyshevVisibility(moments, distance / volume.selectionParams.w);
}

float3 HikariLoadLightProbeCoefficient(uint providerSlot, uint coefficientIndex, uint3 coordinate)
{
    return providerSlot == 0u
        ? gLightProbeShVolume0[coefficientIndex].Load(int4(coordinate, 0)).rgb
        : gLightProbeShVolume1[coefficientIndex].Load(int4(coordinate, 0)).rgb;
}

// 焼き込み volume：面上の点は深度 moment の誤差で自分の面に遮られたと判定される。視線を主に法線を少し混ぜた向き（面の表）へ
// probe 間隔に比例して離してから補間と可視性を評価する（DDGI と RTXGI の surface bias）。法線が主だと部屋の内角で
// 隣の壁から離れず、壁の向こうの probe が可視性をすり抜けて交線に probe 間隔の鋸歯が出る。
float3 HikariProbeSamplePosition(HikariIrradianceVolumeData volume, float3 worldPos, float3 n, float3 v)
{
    const float minimumSpacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
    return worldPos + (n * 0.2f + v * 0.8f) * (0.75f * minimumSpacing * clamp(volume.selectionParams.z, 0.0f, 0.5f));
}

// 焼き込み volume（SH9）。RGB は E/pi、A は sample validity。空間 coverage は influence が所有する。
float4 HikariEvaluateLightProbeVolumeDiffuse(
    uint providerSlot, HikariIrradianceVolumeData volume, float3 worldPos, float3 n, float3 v)
{
    if (!HikariHasIrradianceVolumeData(volume)) return 0.0f.xxxx;
    // 強度ゼロは有効な黒。下位の空へ切り替えると調整結果が逆転する。
    if (volume.spacingIntensity.w == 0.0f) return float4(0,0,0,1);
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 samplePos = HikariProbeSamplePosition(volume, worldPos, n, v);
    const float3 grid = HikariProbeGrid(volume, counts, samplePos);
    const uint3 base = (uint3)floor(grid);
    const float3 fraction = frac(grid);
    uint3 coordinates[8];
    float weights[8];
    float totalWeight = 0.0f;
    bool allVisible = true;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        coordinates[corner] = min(base + offset, counts - 1u);
        const float3 axisWeight = lerp(1.0f.xxx - fraction, fraction, (float3)offset);
        const float interpolationWeight = axisWeight.x * axisWeight.y * axisWeight.z;
        const float visibility = interpolationWeight > 0.0f
            ? HikariLightProbeVisibility(volume, coordinates[corner], samplePos) : 1.0f;
        // 全 probe が遮られた点は補間の重みへ戻す。黒にすると地面など probe 層に重なる面が丸ごと暗くなる。
        weights[corner] = interpolationWeight * max(visibility, 1e-6f);
        totalWeight += weights[corner];
        allVisible = allVisible && visibility >= 0.9999f;
    }
    float3 result = 0.0f.xxx;
    [branch] if (allVisible)
    {
        const float3 uvw = (grid + 0.5f) / (float3)counts;
        [unroll] for (uint coefficient = 0u; coefficient < 9u; ++coefficient)
            result += HikariSampleLightProbeCoefficient(providerSlot, coefficient, uvw) *
                HikariSh9DiffuseWeight(n, coefficient);
    }
    else if (volume.originSampling.w >= 1.5f)
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
        // Full: probe 単位で畳み込み、visibility の分岐を係数ごとに反復しない。
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
    return float4(max(result, 0.0f.xxx), 1.0f);
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

// 焼き込み provider。RGB は coverage 適用済みの E/pi、A は coverage。
// 二つの provider は同じ評価を回す。展開すると slot が実行時の値のまま評価が二重に複製され、forward の
// pixel shader が数万命令に膨らんで PSO の初回生成に数秒かかる。
float4 HikariResolveBakedLightProbeDiffuse(float3 worldPos, float3 n, float3 v)
{
    float4 first = 0.0f.xxxx;
    float4 second = 0.0f.xxxx;
    const float priority0 = gIrradianceVolumes[0].selectionParams.x;
    const float priority1 = gIrradianceVolumes[1].selectionParams.x;
    const bool ordered = priority0 != priority1;
    const uint firstSlot = priority1 > priority0 ? 1u : 0u;
    [loop] for (uint order = 0u; order < 2u; ++order)
    {
        const uint slot = firstSlot ^ order;
        const HikariIrradianceVolumeData volume = gIrradianceVolumes[slot];
        const float influence = HikariEvaluateIrradianceVolumeInfluence(volume, worldPos);
        // rgb は強度適用済みの E/pi、a は influence と sample validity の重み。
        float4 weighted = 0.0f.xxxx;
        [branch] if (influence > 0.0001f)
        {
            const float4 sample = HikariEvaluateLightProbeVolumeDiffuse(slot, volume, worldPos, n, v);
            weighted = float4(sample.rgb * max(0.0f, volume.spacingIntensity.w), influence * sample.a);
        }
        if (order == 0u)
        {
            first = weighted;
            // 優先 provider が全面を覆うなら、隠れた下位 provider の SH を読まない。
            [branch] if (ordered && first.a >= 1.0f)
                return float4(first.rgb, 1.0f);
        }
        else
        {
            second = weighted;
        }
    }
    if (first.a <= 0.0001f && second.a <= 0.0001f) return 0.0f.xxxx;
    const float coverage = saturate(1.0f - (1.0f - first.a) * (1.0f - second.a));
    if (ordered)
        return float4(second.rgb * (second.a * (1.0f - first.a)) + first.rgb * first.a, coverage);
    const float3 localDiffuse = (first.rgb * first.a + second.rgb * second.a) / max(first.a + second.a, 0.0001f);
    return float4(localDiffuse * coverage, coverage);
}

// RGB は coverage 適用済みの E/pi、A は下位（空）を置換する coverage。呼び出し側は A<1 の場合にだけ空の光を評価する。
// 動的 GI の volume（手置きの区域、近景、遠景の優先順に CPU が並べる）を先に重ね、残りを焼き込み provider が埋める。
// skyVisibility は空の見える割合。動的 GI が覆わない部分は 1（遮蔽を知らない）。
float4 HikariResolveLightProbeDiffuse(float3 worldPos, float3 geometricNormal, float3 n, float3 v, out float skyVisibility)
{
    const HikariDynamicGiSample dynamic = HikariQueryDynamicGi(gDynamicGiVolumes,
        HikariSurfaceGiQuery(worldPos, geometricNormal, n, v));
    skyVisibility = saturate(dynamic.skyVisibility + 1.0f - dynamic.coverage);
    if (dynamic.coverage >= 1.0f) return float4(dynamic.value, 1.0f);
    const float4 baked = HikariResolveBakedLightProbeDiffuse(worldPos, n, v);
    return float4(dynamic.value + baked.rgb * (1.0f - dynamic.coverage),
        saturate(dynamic.coverage + baked.a * (1.0f - dynamic.coverage)));
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
