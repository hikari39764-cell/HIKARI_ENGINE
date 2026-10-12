#ifndef HIKARI_DYNAMIC_GI_SAMPLING_HLSLI
#define HIKARI_DYNAMIC_GI_SAMPLING_HLSLI

#include "HIKARI_DynamicGiProbeAtlas.hlsli"

// 位置と幾何の向きは可視性、direction は放射照度の角度だけを決める。toViewer は面から見ている側（相機、ray の出発点）
// への単位ベクトルで、無ければ 0。medium は全方向平均 radiance を返す。面の問い合わせは E/pi を返す。
struct HikariDynamicGiQuery
{
    float3 position;
    float3 surfaceNormal;
    float3 direction;
    float3 toViewer;
    bool medium;
};

HikariDynamicGiQuery HikariSurfaceGiQuery(float3 position, float3 surfaceNormal, float3 direction,
    float3 toViewer = 0.0f.xxx)
{
    HikariDynamicGiQuery query;
    query.position = position;
    query.surfaceNormal = surfaceNormal;
    query.direction = direction;
    query.toViewer = toViewer;
    query.medium = false;
    return query;
}

// value と skyVisibility は coverage 適用前の値。coverage は問い合わせ点を囲む 8 probe のうち活性で初期化済みの
// ものの補間重みの和（0..1）。足りない分は呼び出し側が下位の層か空の光で埋める。confidence は時間的な収束（成熟度）。
struct HikariDynamicGiSample
{
    float3 value;
    float skyVisibility;
    float coverage;
    float confidence;
};

// 補間と可視性を評価する点。面から見ている側と法線へ半分ずつ離す（RTXGI の view bias + normal bias）。法線だけだと
// 部屋の内角で隣の壁の面から離れず、壁の向こうの probe が可視性をすり抜けて交線に色の帯と probe 間隔の鋸歯が出た。
// 見ている側を主にすると（焼き込み volume の 0.8）、掠め角で見る天井の点が面に沿って滑り、probe の列ごとに斜めの
// 明るい筋が出た。見ている側が無い問い合わせは法線だけ。粗い層でも偏移を拡大しない。
float3 HikariDynamicGiSamplePosition(HikariIrradianceVolumeData volume, HikariDynamicGiQuery query)
{
    if (query.medium) return query.position;
    const float spacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
    const float bias = min(spacing, 1.5f) * 0.75f * clamp(volume.selectionParams.z, 0.0f, 0.5f);
    const bool viewed = dot(query.toViewer, query.toViewer) > 0.25f;
    return query.position + (viewed ? query.surfaceNormal * 0.5f + query.toViewer * 0.5f : query.surfaceNormal) * bias;
}

// 小さな重み（壁の向こうで半端に見えると判定された probe）は 3 乗で潰す（RTXGI の weight crush）。
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
    float maturities[8];
    float totalWeight = 0.0f;
    float missing = 0.0f; // 非活性（初期化前）の probe の補間重み。全部活性なら 0 で coverage は正確に 1。
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        const uint3 coordinate = min(base + offset, counts - 1u);
        tiles[corner] = HikariDynamicGiProbeTile(coordinate, counts);
        const float3 axes = lerp(1.0f.xxx - fraction, fraction, (float3)offset);
        const float trilinear = axes.x * axes.y * axes.z;
        const float4 state = HikariDynamicGiProbeState(descriptorIndex, tiles[corner]);
        maturities[corner] = saturate(state.a);
        const float active = maturities[corner] > 0.0f ? 1.0f : 0.0f;
        missing += trilinear * (1.0f - active);
        // probe は格子点から動いている（形状の中や面のすぐ近くから離してある）。
        const float3 probePosition = volume.originSampling.xyz + coordinate * volume.spacingIntensity.xyz + state.rgb;
        const float4 moments = HikariDynamicGiProbeMoments(volume, descriptorIndex, tiles[corner], atlasSize,
            samplePosition - probePosition);
        const float visibility = HikariProbeChebyshevVisibility(moments.xy, moments.w);
        const float3 toProbe = probePosition - query.position;
        // 面の向きによる重み（RTXGI の wrap shading）。裏側の probe も 0 にはしない：0 にすると球の下面のように周りの
        // probe がほぼ裏側になる点で支持が消え、下の層や空の光へ落ちて明るい欠けが出る。壁の向こうの光は可視性で止める。
        const float facing = 0.5f + 0.5f * dot(toProbe, query.surfaceNormal) * rsqrt(max(dot(toProbe, toProbe), 1e-8f));
        const float surfaceWeight = query.medium ? 1.0f : facing * facing + 0.2f;
        const float patch = HikariDynamicGiProbePatchWeight(volume, descriptorIndex, tiles[corner],
            query.position - probePosition);
        weights[corner] = trilinear * active * patch * HikariDynamicGiWeight(visibility, surfaceWeight);
        totalWeight += weights[corner];
    }
    // 可視性は統計なので下限（HikariDynamicGiWeight）を持ち、支持を切らない。重みが完全に消えるのは幾何の証明
    // （面の裏、円盤の向こう、非活性）だけで、その点はこの層の支持なしとして下位の層か空へ渡す。
    result.coverage = totalWeight > 0.0f ? saturate(1.0f - missing) : 0.0f;
    if (totalWeight <= 0.0f) return result;

    const float3 directions[4] = {float3(1,1,1), float3(1,-1,-1), float3(-1,1,-1), float3(-1,-1,1)};
    float3 irradiance[4] = {0.0f.xxx,0.0f.xxx,0.0f.xxx,0.0f.xxx};
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const float weight = weights[corner];
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
        }
    }
    // 正四面体の四方向は 2 次までの球面調和を正しく平均するので、E/pi の四方向の平均は入射 radiance の全方向の平均。
    [unroll] for (uint direction = 0u; direction < (query.medium ? 4u : 1u); ++direction)
    {
        const float3 value = irradiance[direction] / totalWeight;
        result.value += value * value * (query.medium ? 0.25f : 1.0f);
    }
    result.value *= max(volume.spacingIntensity.w, 0.0f);
    result.skyVisibility = saturate(result.skyVisibility / totalWeight);
    result.confidence = saturate(result.confidence / totalWeight);
    return result;
}

// 全呼び出し側で同じ優先順と領域境界を使う。返す value と skyVisibility は coverage 適用済み。
// 上位の層（手置きの区域、近景）の覆わない分を下位の層が埋め、最後に残った 1 - coverage を呼び出し側が空の光で埋める。
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
