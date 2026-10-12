#ifndef HIKARI_DYNAMIC_GI_SAMPLING_HLSLI
#define HIKARI_DYNAMIC_GI_SAMPLING_HLSLI

#include "HIKARI_DynamicGiProbeAtlas.hlsli"

// 位置と幾何の向きは可視性、direction は放射照度の角度だけを決める。
// medium は全方向平均 radiance を返す。面の問い合わせは E/pi を返す。
struct HikariDynamicGiQuery
{
    float3 position;
    float3 surfaceNormal;
    float3 direction;
    bool medium;
};

HikariDynamicGiQuery HikariSurfaceGiQuery(float3 position, float3 surfaceNormal, float3 direction)
{
    HikariDynamicGiQuery query;
    query.position = position;
    query.surfaceNormal = surfaceNormal;
    query.direction = direction;
    query.medium = false;
    return query;
}

struct HikariDynamicGiSample
{
    float3 value;
    float skyVisibility;
    float coverage;
    float confidence; // 補間の支持と時間的な収束。有効な黒と、支持の無い問い合わせを区別する。
};

float3 HikariDynamicGiSamplePosition(HikariIrradianceVolumeData volume, HikariDynamicGiQuery query)
{
    // 相機や反射方向を動かしても同じ面を問い合わせる。粗い層でも偏移を拡大しない。
    const float spacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
    const float bias = min(spacing, 1.5f) * 0.75f * clamp(volume.selectionParams.z, 0.0f, 0.5f);
    return query.position + (query.medium ? 0.0f.xxx : query.surfaceNormal * bias);
}

float HikariDynamicGiWeight(float visibility, float surfaceWeight)
{
    const float weight = max(visibility, 0.05f) * surfaceWeight;
    return weight < 0.2f ? weight * weight * weight / 0.04f : weight;
}

HikariDynamicGiSample HikariEvaluateDynamicGiVolume(HikariIrradianceVolumeData volume, HikariDynamicGiQuery query)
{
    HikariDynamicGiSample result = (HikariDynamicGiSample)0;
    if (!HikariHasIrradianceVolumeData(volume)) return result;
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 samplePosition = HikariDynamicGiSamplePosition(volume, query);
    const float3 grid = HikariProbeGrid(volume, counts, samplePosition);
    const uint3 base = (uint3)floor(grid);
    const float3 fraction = frac(grid);
    const uint descriptorIndex = (uint)(volume.selectionParams.y + 0.5f) - 1u;
    const float2 atlasSize = HikariDynamicGiAtlasSize(counts);
    float weights[8];
    float2 tiles[8];
    uint3 coordinates[8];
    uint links[8];
    float validCoverage = 0.0f;
    float initializedCoverage = 0.0f;
    float maturities[8];
    float totalPreference = 0.0f;
    float bestPreference = -1.0f;
    uint reference = 0u;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        coordinates[corner] = min(base + offset, counts - 1u);
        tiles[corner] = HikariDynamicGiProbeTile(coordinates[corner], counts);
        const float3 axes = lerp(1.0f.xxx - fraction, fraction, (float3)offset);
        const float3 probePosition = volume.originSampling.xyz + coordinates[corner] * volume.spacingIntensity.xyz;
        const float4 moments = HikariDynamicGiProbeMoments(volume, descriptorIndex, tiles[corner], atlasSize,
            samplePosition - probePosition);
        const float3 toProbe = probePosition - query.position;
        const float facing = 0.5f + 0.5f * dot(toProbe, query.surfaceNormal) * rsqrt(max(dot(toProbe, toProbe), 1e-8f));
        const float visibility = HikariProbeChebyshevVisibility(moments.xy,moments.w);
        // 受光面の接平面は世界空間の幾何。表側の問い合わせに、裏側の probe の光を混ぜない。
        // view やサンプル方向では動かさず、反射光の更新にも同じ規則を適用する。
        const float minimumSpacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
        const float frontSide = query.medium ? 1.0f :
            smoothstep(-0.15f, -0.05f, dot(toProbe, query.surfaceNormal) / minimumSpacing);
        const float surfaceWeight = query.medium ? 1.0f : (facing * facing + 0.2f) * frontSide;
        const bool patchVisible = HikariDynamicGiProbePatchVisible(volume,descriptorIndex,tiles[corner],
            query.position-probePosition);
        const float interpolation = axes.x * axes.y * axes.z * moments.z * (patchVisible ? 1.0f : 0.0f);
        // bias は統計的な自己遮蔽を避ける。実際の受光点が形状を越えた時は、有限の幾何 patch が拒否する。
        const float preference = interpolation * HikariDynamicGiWeight(visibility,surfaceWeight);
        if (preference > bestPreference) { bestPreference = preference; reference = corner; }
        // moment は有限標本の統計。小さな値で支持を切断せず、幾何の遮断だけを不連続に扱う。
        weights[corner] = preference;
        links[corner] = HikariDynamicGiProbeLinks(descriptorIndex, tiles[corner], corner);
        totalPreference += preference;
        const float validWeight = axes.x * axes.y * axes.z * moments.z;
        validCoverage += validWeight;
        maturities[corner] = gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Load(
            int3(int2(tiles[corner]) + int2(10,0),0)).a;
        initializedCoverage += axes.x * axes.y * axes.z * (maturities[corner] > 0.0f ? 1.0f : 0.0f);
    }
    // 初期化済みの領域は空へ戻さない。幾何の支持が無ければ confidence=0 で下位層を試す。
    result.coverage = initializedCoverage > 0.0f ? 1.0f : 0.0f;
    if (validCoverage <= 0.0f || totalPreference <= 0.0f) return result;

    // 世界位置・幾何・可視性で基準を選び、その基準から形状を越えずに到達する角だけを混ぜる。
    // 遮断された別室の群を少量でも平均すると、室外の強い光が漏れる。相機方向は選択に使わない。

    const float3 directions[4] = {float3(1,1,1), float3(1,-1,-1), float3(-1,1,-1), float3(-1,-1,1)};
    float3 irradiance[4] = {0.0f.xxx,0.0f.xxx,0.0f.xxx,0.0f.xxx};
    float totalWeight = 0.0f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const bool3 different = coordinates[corner] != coordinates[reference];
        const uint axes = uint(different.x) | (uint(different.y) << 1u) | (uint(different.z) << 2u);
        const uint bit = 1u << (max(axes,1u) - 1u);
        const bool connected = axes == 0u || (links[corner] & links[reference] & bit) != 0u;
        const float weight = connected ? weights[corner] : 0.0f;
        [branch] if (weight > 0.0f)
        {
            [unroll] for (uint direction = 0u; direction < (query.medium ? 4u : 1u); ++direction)
            {
                const float2 uv = HikariEncodeOctahedralDirection(query.medium ? directions[direction] : query.direction);
                const float4 value = HikariSampleDynamicGiIrradiance(descriptorIndex, tiles[corner], atlasSize, uv);
                irradiance[direction] += HikariDynamicGiBlendSpace(value.rgb) * weight;
                if (direction == 0u) result.skyVisibility += value.a * weight;
            }
            result.confidence += maturities[corner] * weight;
            // 可視性は補間の支持であり、面を暗くする AO ではない。有効な支持内で正規化する。
            totalWeight += weight;
        }
    }
    if (totalWeight <= 0.0f) return result;
    [unroll] for (uint direction = 0u; direction < (query.medium ? 4u : 1u); ++direction)
    {
        const float3 value = irradiance[direction] / max(totalWeight, 1e-30f);
        result.value += value * value * (query.medium ? 0.25f : 1.0f);
    }
    result.value *= max(volume.spacingIntensity.w, 0.0f);
    result.skyVisibility = saturate(result.skyVisibility / max(totalWeight, 1e-30f));
    result.confidence = saturate(result.confidence / max(totalWeight, 1e-30f));
    result.coverage = 1.0f; // 遮られた黒も有効。光の強さでデータの有無を判定しない。
    return result;
}

// 全呼び出し側で同じ優先順と領域境界を使う。返す value と sky は coverage 適用済み。
HikariDynamicGiSample HikariQueryDynamicGi(HikariIrradianceVolumeData volumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT],
    HikariDynamicGiQuery query)
{
    HikariDynamicGiSample samples[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    float influences[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    uint last = HIKARI_DYNAMIC_GI_VOLUME_COUNT - 1u;
    [loop] for (uint index = 0u; index < HIKARI_DYNAMIC_GI_VOLUME_COUNT; ++index)
    {
        influences[index] = HikariEvaluateIrradianceVolumeInfluence(volumes[index], query.position);
        samples[index] = (HikariDynamicGiSample)0;
        [branch] if (influences[index] > 0.0f)
            samples[index] = HikariEvaluateDynamicGiVolume(volumes[index], query);
        // 収束済みの全面 coverage は下位層を必要としない。
        if (influences[index] * samples[index].coverage >= 1.0f && samples[index].confidence >= 1.0f)
        {
            last = index;
            break;
        }
    }
    HikariDynamicGiSample result = (HikariDynamicGiSample)0;
    // 未収束の上位層は有効な下位層から受け渡す。下位も無い時は初期標本を使い、空へ漏らさない。
    [loop] for (int index = (int)last; index >= 0; --index)
    {
        const float coverage = influences[index] * samples[index].coverage;
        const float weight = coverage * lerp(1.0f, samples[index].confidence, result.coverage);
        result.value = result.value * (1.0f - weight) + samples[index].value * weight;
        result.skyVisibility = result.skyVisibility * (1.0f - weight) + samples[index].skyVisibility * weight;
        result.coverage = result.coverage * (1.0f - weight) + weight;
    }
    return result;
}

#endif
