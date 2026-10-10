#ifndef HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI
#define HIKARI_LIGHTING_PROVIDER_SAMPLING_HLSLI

#include "HIKARI_DynamicGiProbeAtlas.hlsli"

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
    const float2 moments = gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleLevel(gLinearClamp,
        HikariProbeAtlasUv(tile, side, HikariEncodeOctahedralDirection(offset / distance), atlasSize), 0.0f).rg;
    return HikariProbeChebyshevVisibility(moments, distance / volume.selectionParams.w);
}

float3 HikariLoadLightProbeCoefficient(uint providerSlot, uint coefficientIndex, uint3 coordinate)
{
    return providerSlot == 0u
        ? gLightProbeShVolume0[coefficientIndex].Load(int4(coordinate, 0)).rgb
        : gLightProbeShVolume1[coefficientIndex].Load(int4(coordinate, 0)).rgb;
}

// 面上の点は深度 moment の誤差で自分の面に遮られたと判定される。視線を主に法線を少し混ぜた向き（面の表）へ
// probe 間隔に比例して離してから補間と可視性を評価する（DDGI と RTXGI の surface bias）。法線が主だと部屋の内角で
// 隣の壁から離れず、壁の向こうの probe が可視性をすり抜けて交線に probe 間隔の鋸歯が出る。
float3 HikariProbeSamplePosition(HikariIrradianceVolumeData volume, float3 worldPos, float3 n, float3 v)
{
    const float minimumSpacing = min(volume.spacingIntensity.x, min(volume.spacingIntensity.y, volume.spacingIntensity.z));
    return worldPos + (n * 0.2f + v * 0.8f) * (0.75f * minimumSpacing * clamp(volume.selectionParams.z, 0.0f, 0.5f));
}

// 小さな重み（壁の向こうで半端に見えると判定された probe）は 3 乗で潰す（RTXGI の weight crush）。
float HikariCrushProbeWeight(float weight)
{
    return weight < 0.2f ? weight * weight * weight / 0.04f : weight;
}

// 動的 GI の volume。RGB は E/pi、A は coverage。v は受光点から視点への方向。skyVisibility は
// 法線まわりの空の見える割合（probe 間の重みで補間）。
// probe ごとに atlas の深度 moment と validity、放射照度を一度ずつ読む（SH9 の 9 係数を読まない）。
// 動的 probe は ray が疎で深度 moment が粗いので、面の裏側の probe を滑らかに弱める（DDGI の backface 項）。
// 画面の面（referenceProbe が負）は深度 moment の可視性だけで角を重み付ける。連通で角を落とすと、基準の角が隣の格子で
// 変わる所で共有面の角が片側で落とされ片側で残り、格子の面に沿った楔形の段差（球の下面の黒い楔）になる（画面で連通を
// 外しても GIReference / CornellBox の室内は 1/255 も明るくならない）。
// referenceProbe は probe の ray の当たった点（照り返しが probe に溜まる所）を評価する時のその probe の格子座標。ここでは
// 壁を越えて光を運んではならない。白い閉じた部屋は反射を重ねて光を何倍にも増やすので、壁の向こうの日なたの角がわずかに
// 混ざるだけで昼の部屋が太陽の当たる壁の側から明るくなった。深度 moment の可視性は壁沿いの掠める向きで深度が混ざって
// 緩むので、角の選び方に使わない。撃った probe が当たった点の格子の角にあれば、そこから連通している（形状が塞いでいない）
// 角だけを使う。無ければ（ray が一格子より遠くへ届いた）撃った probe 自身の放射照度（当たった面の向き）で近似する。撃った
// probe は当たった点と同じ側にあることが確か。
float4 HikariEvaluateDynamicGiVolume(HikariIrradianceVolumeData volume, float3 worldPos, float3 n, float3 v,
    int3 referenceProbe, out float skyVisibility)
{
    skyVisibility = 1.0f;
    if (!HikariHasIrradianceVolumeData(volume)) return 0.0f.xxxx;
    if (volume.spacingIntensity.w == 0.0f) return float4(0,0,0,1);
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 samplePos = HikariProbeSamplePosition(volume, worldPos, n, v);
    const float3 grid = HikariProbeGrid(volume, counts, samplePos);
    const uint descriptorIndex = (uint)(volume.selectionParams.y + 0.5f) - 1u;
    const float2 atlasSize = HikariDynamicGiAtlasSize(counts);
    const float2 normalUv = HikariEncodeOctahedralDirection(n);
    const uint3 base = (uint3)floor(grid);
    const float3 fraction = frac(grid);
    const bool probeRay = referenceProbe.x >= 0;
    // 撃った probe の連通 7 bit（当たった点の格子の他の角の向きの卦限）。画面の面では使わない。
    uint shooterLinks = 0u;
    [branch] if (probeRay)
    {
        const int3 low = int3(min(base, counts - 1u));
        const int3 high = int3(min(base + 1u, counts - 1u));
        const bool3 atLow = referenceProbe == low;
        const bool3 atHigh = referenceProbe == high;
        const float2 shooterTile = HikariDynamicGiProbeTile((uint3)referenceProbe, counts);
        [branch] if (!((atLow.x || atHigh.x) && (atLow.y || atHigh.y) && (atLow.z || atHigh.z)))
        {
            const float4 own = HikariSampleDynamicGiIrradiance(descriptorIndex, shooterTile, atlasSize, normalUv);
            skyVisibility = saturate(own.a);
            return float4(pow(max(own.rgb, 0.0f.xxx), kIrradianceGamma), 1.0f);
        }
        shooterLinks = HikariDynamicGiProbeLinks(descriptorIndex, shooterTile,
            (atLow.x ? 0u : 1u) | (atLow.y ? 0u : 2u) | (atLow.z ? 0u : 4u));
    }
    float cornerWeights[8];
    float2 cornerTiles[8];
    float validWeight = 0.0f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        const uint3 coordinate = min(base + offset, counts - 1u);
        const float3 axisWeight = lerp(1.0f.xxx - fraction, fraction, (float3)offset);
        const float interpolationWeight = axisWeight.x * axisWeight.y * axisWeight.z;
        cornerWeights[corner] = 0.0f;
        cornerTiles[corner] = HikariDynamicGiProbeTile(coordinate, counts);
        [branch] if (interpolationWeight > 0.0f)
        {
            const float3 probePosition = volume.originSampling.xyz + (float3)coordinate * volume.spacingIntensity.xyz;
            const float4 moments = HikariDynamicGiProbeMoments(volume, descriptorIndex, cornerTiles[corner], atlasSize,
                samplePos - probePosition);
            const float3 toProbe = probePosition - worldPos;
            const float facing = (dot(toProbe, n) * rsqrt(max(dot(toProbe, toProbe), 1e-8f)) + 1.0f) * 0.5f;
            // 全 probe が遮られた点は補間の重みへ戻す。黒にすると地面など probe 層に重なる面が丸ごと暗くなる。
            const float visibility = HikariProbeChebyshevVisibility(moments.xy, moments.w);
            const float crushed = HikariCrushProbeWeight(visibility * (facing * facing + 0.2f));
            cornerWeights[corner] = interpolationWeight * max(crushed, 1e-6f) * moments.z;
            validWeight += interpolationWeight * moments.z;
            // 撃った probe から連通していない角を落とす（軸の差の組 x=1, y=2, z=4 から 1 を引いた bit）。
            const uint axes = ((int)coordinate.x != referenceProbe.x ? 1u : 0u) |
                ((int)coordinate.y != referenceProbe.y ? 2u : 0u) | ((int)coordinate.z != referenceProbe.z ? 4u : 0u);
            if (probeRay && axes != 0u && (shooterLinks & (1u << (axes - 1u))) == 0u)
                cornerWeights[corner] = 0.0f;
        }
    }
    if (validWeight <= 1e-5f) return 0.0f.xxxx;
    float4 result = 0.0f.xxxx;
    float totalWeight = 0.0f;
    [unroll] for (uint linked = 0u; linked < 8u; ++linked)
    {
        [branch] if (cornerWeights[linked] > 0.0f)
        {
            result += HikariSampleDynamicGiIrradiance(descriptorIndex, cornerTiles[linked], atlasSize, normalUv) *
                cornerWeights[linked];
            totalWeight += cornerWeights[linked];
        }
    }
    result /= max(totalWeight, 1e-20f);
    result.rgb = pow(max(result.rgb, 0.0f.xxx), kIrradianceGamma);
    skyVisibility = saturate(result.a);
    // 壁や屋根の中の無効な probe の分を下位（空の光）へ回すと、壁際に遮られない空の光が差し込む。有効な probe
    // が少しでもあればその間で正規化して全面を覆い、ほぼ全部が無効な時だけ下位へ移す。
    return float4(result.rgb, saturate(validWeight * 50.0f));
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
float4 HikariResolveLightProbeDiffuse(float3 worldPos, float3 n, float3 v, out float skyVisibility)
{
    float3 dynamicDiffuse = 0.0f.xxx;
    float dynamicCoverage = 0.0f;
    float dynamicSky = 0.0f;
    [loop] for (uint index = 0u; index < HIKARI_DYNAMIC_GI_VOLUME_COUNT; ++index)
    {
        const HikariIrradianceVolumeData volume = gDynamicGiVolumes[index];
        const float influence = HikariEvaluateIrradianceVolumeInfluence(volume, worldPos);
        [branch] if (influence > 0.0001f)
        {
            float sky;
            const float4 sample = HikariEvaluateDynamicGiVolume(volume, worldPos, n, v, int3(-1, -1, -1), sky);
            const float weight = influence * sample.a * (1.0f - dynamicCoverage);
            dynamicDiffuse += sample.rgb * max(0.0f, volume.spacingIntensity.w) * weight;
            dynamicSky += sky * weight;
            dynamicCoverage += weight;
            // 上位の volume が全面を覆うなら、下位の volume と焼き込みを読まない。
            [branch] if (dynamicCoverage >= 0.9999f)
            {
                skyVisibility = saturate(dynamicSky / dynamicCoverage);
                return float4(dynamicDiffuse, 1.0f);
            }
        }
    }
    skyVisibility = saturate(dynamicSky + (1.0f - dynamicCoverage));
    const float4 baked = HikariResolveBakedLightProbeDiffuse(worldPos, n, v);
    return float4(dynamicDiffuse + baked.rgb * (1.0f - dynamicCoverage),
        saturate(dynamicCoverage + baked.a * (1.0f - dynamicCoverage)));
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
