#ifndef HIKARI_DYNAMIC_GI_PROBE_ATLAS_HLSLI
#define HIKARI_DYNAMIC_GI_PROBE_ATLAS_HLSLI

// 動的 GI の probe 格子と probe atlas の読み方。面の受光（HIKARI_LightingProviderSampling.hlsli）と媒質（体積霧）が
// 共有する。include する側が gMaterialTexturePool、HIKARI_MATERIAL_TEXTURE_POOL_COUNT、gLinearClamp を宣言する。
#include "HIKARI_IrradianceSh9.hlsli"
#include "../HIKARI_DirectionEncoding.hlsli"

// 動的 GI の probe atlas（RGBA16F）。probe ごとに幅 28、高さ 18 の tile を持つ。x=[0,10)、y=[0,10) は放射照度 E/pi の
// 1/5 乗（A は余弦で重みづけた空の見える割合）の 8x8 八面体、x=[10,28) は深度 moment（RG）と validity（B、tile 内で一定）の
// 16x16 八面体（どちらも境界 1 texel 込み）。深度は鋭い波瓣（約 10 度）に合わせて細かく取る。8x8（約 22 度間隔）だと
// 壁の縁を抜ける方向の深度が補間で混ざり、部屋の内角で壁の向こうの probe が半端に見えて鋸歯状に漏れる。
// 深度 tile の (11 + 卦限, 1) texel の A は、その卦限の隣の 7 probe との間を形状が塞いでいないかの 7 bit（bit は軸の
// 差の組 x=1, y=2, z=4 から 1 を引いた位置）。
static const uint HIKARI_DYNAMIC_GI_TILE_WIDTH = 28u;
static const uint HIKARI_DYNAMIC_GI_TILE_HEIGHT = 18u;
static const float HIKARI_DYNAMIC_GI_OCTAHEDRAL_SIDE = 8.0f;
static const float HIKARI_DYNAMIC_GI_DEPTH_SIDE = 16.0f;
// 放射照度は E/pi の 1/5 乗で保存する（暗い所の精度を保つ）。補間は RTXGI の DDGIGetVolumeIrradiance と同じく 1/2 乗の
// 空間で行い、最後に二乗して戻す。1/5 乗のまま混ぜると暗い probe へ強く引かれ、明暗の境の補間が線形の平均よりずっと
// 暗くなる（半々なら線形 0.525、1/2 乗 0.374、1/5 乗 0.278）。壁際や開口のまわりが汚く暗くなり、probe の配置が違う
// 画質の档位ごとに明るさがずれた。
static const float kIrradianceGamma = 5.0f;

// 保存値（E/pi の 1/5 乗）を補間の空間（E/pi の 1/2 乗）へ。
float3 HikariDynamicGiBlendSpace(float3 stored)
{
    return pow(max(stored, 0.0f.xxx), kIrradianceGamma * 0.5f);
}

// 境界 1 texel を含む八面体 tile 内の方向の uv。
float2 HikariProbeAtlasUv(float2 tileTexel, float side, float2 directionUv, float2 atlasSize)
{
    return (tileTexel + 1.0f + directionUv * side) / atlasSize;
}

// 深度 moment（距離を visibility scale で正規化）に対する Chebyshev 上界。
float HikariProbeChebyshevBound(float2 moments, float normalizedDistance)
{
    const float delta = max(0.0f, normalizedDistance - moments.x);
    const float variance = max(0.0f, moments.y - moments.x * moments.x);
    return delta <= 0.0f ? 1.0f : variance / max(variance + delta * delta, 1e-20f);
}

// 面の受光の可視性は上界の 3 乗（半端に見える probe を強く弱める）。
float HikariProbeChebyshevVisibility(float2 moments, float normalizedDistance)
{
    const float visibility = HikariProbeChebyshevBound(moments, normalizedDistance);
    return visibility * visibility * visibility;
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

float3 HikariProbeGrid(HikariIrradianceVolumeData volume, uint3 counts, float3 samplePos)
{
    return clamp((samplePos - volume.originSampling.xyz) / max(volume.spacingIntensity.xyz, 0.0001f.xxx),
        0.0f.xxx, (float3)(counts - 1u));
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

float2 HikariDynamicGiAtlasSize(uint3 counts)
{
    return float2(counts.x * counts.y * HIKARI_DYNAMIC_GI_TILE_WIDTH, counts.z * HIKARI_DYNAMIC_GI_TILE_HEIGHT);
}

// probe の tile の左上の texel。
float2 HikariDynamicGiProbeTile(uint3 coordinate, uint3 counts)
{
    return (float2)(uint2(coordinate.x + coordinate.y * counts.x, coordinate.z) *
        uint2(HIKARI_DYNAMIC_GI_TILE_WIDTH, HIKARI_DYNAMIC_GI_TILE_HEIGHT));
}

// probe から toSample の向きの深度 moment（xy）、probe の validity（z）、visibility scale で正規化した距離（w）。
float4 HikariDynamicGiProbeMoments(HikariIrradianceVolumeData volume, uint descriptorIndex, float2 tile,
    float2 atlasSize, float3 toSample)
{
    const float distance = max(length(toSample), 1e-6f);
    const float3 depth = gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleLevel(
        gLinearClamp, HikariProbeAtlasUv(tile + float2(HIKARI_DYNAMIC_GI_OCTAHEDRAL_SIDE + 2.0f, 0.0f),
            HIKARI_DYNAMIC_GI_DEPTH_SIDE, HikariEncodeOctahedralDirection(toSample / distance), atlasSize), 0.0f).rgb;
    return float4(depth.rg, saturate(depth.b), distance / volume.selectionParams.w);
}

// probe の放射照度（E/pi の 1/kIrradianceGamma 乗。A は余弦で重みづけた空の見える割合）。
float4 HikariSampleDynamicGiIrradiance(uint descriptorIndex, float2 tile, float2 atlasSize, float2 directionUv)
{
    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleLevel(gLinearClamp,
        HikariProbeAtlasUv(tile, HIKARI_DYNAMIC_GI_OCTAHEDRAL_SIDE, directionUv, atlasSize), 0.0f);
}

// probe の卦限 octant（隣の probe がある向き。bit が立つ軸は負の向き）の連通 7 bit。
uint HikariDynamicGiProbeLinks(uint descriptorIndex, float2 tile, uint octant)
{
    return (uint)(gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Load(
        int3(int2(tile) + int2(11 + (int)octant, 1), 0)).a + 0.5f);
}

// 補間の基準の角 reference から角 corner までを形状が塞いでいないか（角の番号は x=1, y=2, z=4 の bit）。
// 深度 moment の可視性は壁に沿う斜めの向きで深度が混ざって緩くなり、probe 間隔より薄い壁の向こうの probe を拾う。
// 基準（最も重い角）と塞がれた角を混ぜないことで、壁の向こうの光を断つ。
bool HikariDynamicGiCornerLinked(uint links, uint reference, uint corner)
{
    const uint axes = reference ^ corner;
    return axes == 0u || (links & (1u << (axes - 1u))) != 0u;
}

// 媒質（霧）の一点へ届く全方向の平均 radiance。正四面体の四方向は 2 次までの球面調和を正しく平均するので、
// 2 次までに収まる E/pi の四方向の平均は入射 radiance の全方向の平均に等しい。面ではないので面の向きの重みと
// 標本点のずらしは掛けない。可視性は Chebyshev の上界そのもの（壁の向こうの probe だけを落とす）。面の受光の
// 3 乗と小さな重みの潰しは面の自己遮蔽を抑えるためのもので、媒質では probe ごとの判定の切り替わりが斑に見える。
// plane（法線, 定数項）は標本点の側の面（視線が最初に当たる面）。その裏の probe は面を挟んだ反対側なので、表の probe が
// あれば使わない。間隔 6m の遠景が薄い屋根を挟むと、掠める向きの屋内の probe の深度の分布が屋根の上の点を見えると判定し、
// 屋根の上の霧が灯りの色で光った。法線が 0 なら判定しない。
// RGB は強度を掛ける前、A は coverage（HikariEvaluateDynamicGiVolume と同じ意味）。
float4 HikariEvaluateDynamicGiMedium(HikariIrradianceVolumeData volume, float3 worldPos, float4 plane)
{
    if (!HikariHasIrradianceVolumeData(volume)) return 0.0f.xxxx;
    if (volume.spacingIntensity.w == 0.0f) return float4(0,0,0,1);
    const uint3 counts = (uint3)(volume.countsProbeCount.xyz + 0.5f);
    const float3 grid = HikariProbeGrid(volume, counts, worldPos);
    const uint descriptorIndex = (uint)(volume.selectionParams.y + 0.5f) - 1u;
    const float2 atlasSize = HikariDynamicGiAtlasSize(counts);
    const float2 directionUv[4] = {
        HikariEncodeOctahedralDirection(float3(1.0f, 1.0f, 1.0f)),
        HikariEncodeOctahedralDirection(float3(1.0f, -1.0f, -1.0f)),
        HikariEncodeOctahedralDirection(float3(-1.0f, 1.0f, -1.0f)),
        HikariEncodeOctahedralDirection(float3(-1.0f, -1.0f, 1.0f))};
    const uint3 base = (uint3)floor(grid);
    const float3 fraction = frac(grid);
    float cornerWeights[8];
    float2 cornerTiles[8];
    float validWeight = 0.0f;
    float frontWeight = 0.0f;
    // 基準の候補は面の表と全体の二通り。表に有効な probe があれば表から選ぶ。
    float2 strongest = -1.0f.xx;
    uint2 references = 0u.xx;
    float4 referenceTiles = 0.0f.xxxx;
    uint front = 0u;
    const float planeTolerance = 0.1f * max(volume.spacingIntensity.x, max(volume.spacingIntensity.y, volume.spacingIntensity.z));
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
                worldPos - probePosition);
            cornerWeights[corner] =
                interpolationWeight * max(HikariProbeChebyshevBound(moments.xy, moments.w), 1e-4f) * moments.z;
            validWeight += interpolationWeight * moments.z;
            const bool inFront = dot(plane.xyz, probePosition) + plane.w >= -planeTolerance;
            front |= inFront ? (1u << corner) : 0u;
            frontWeight += inFront ? interpolationWeight * moments.z : 0.0f;
            // 基準は 3 乗の可視性（面と同じ厳しさ）で選ぶ。緩い可視性のままだと壁際で壁の向こうの probe が基準になる。
            const float preference =
                interpolationWeight * HikariProbeChebyshevVisibility(moments.xy, moments.w) * moments.z;
            if (preference > strongest.x)
            {
                strongest.x = preference;
                references.x = corner;
                referenceTiles.xy = cornerTiles[corner];
            }
            if (inFront && preference > strongest.y)
            {
                strongest.y = preference;
                references.y = corner;
                referenceTiles.zw = cornerTiles[corner];
            }
        }
    }
    if (validWeight <= 1e-5f) return 0.0f.xxxx;
    const bool frontOnly = frontWeight > 1e-5f;
    const uint reference = frontOnly ? references.y : references.x;
    const uint usable = frontOnly ? front : 0xffu;
    const uint links =
        HikariDynamicGiProbeLinks(descriptorIndex, frontOnly ? referenceTiles.zw : referenceTiles.xy, reference);
    float3 irradiance[4] = {0.0f.xxx, 0.0f.xxx, 0.0f.xxx, 0.0f.xxx};
    float totalWeight = 0.0f;
    [unroll] for (uint linked = 0u; linked < 8u; ++linked)
    {
        [branch] if (cornerWeights[linked] > 0.0f && (usable & (1u << linked)) != 0u &&
            HikariDynamicGiCornerLinked(links, reference, linked))
        {
            [unroll] for (uint direction = 0u; direction < 4u; ++direction)
                irradiance[direction] += HikariDynamicGiBlendSpace(HikariSampleDynamicGiIrradiance(descriptorIndex,
                    cornerTiles[linked], atlasSize, directionUv[direction]).rgb) * cornerWeights[linked];
            totalWeight += cornerWeights[linked];
        }
    }
    float3 radiance = 0.0f.xxx;
    [unroll] for (uint direction = 0u; direction < 4u; ++direction)
    {
        const float3 blended = irradiance[direction] / max(totalWeight, 1e-20f);
        radiance += blended * blended;
    }
    return float4(radiance * 0.25f, saturate(validWeight * 50.0f));
}

#endif
