#ifndef HIKARI_DYNAMIC_GI_PROBE_ATLAS_HLSLI
#define HIKARI_DYNAMIC_GI_PROBE_ATLAS_HLSLI

// 動的 GI の probe 格子と atlas の低水準読み取り。補間と provider 合成は HIKARI_DynamicGiSampling.hlsli が所有する。
// include する側が gMaterialTexturePool、HIKARI_MATERIAL_TEXTURE_POOL_COUNT、gLinearClamp を宣言する。
#include "HIKARI_IrradianceSh9.hlsli"
#include "../HIKARI_DirectionEncoding.hlsli"

// 動的 GI の probe atlas（RGBA16F）。probe ごとに幅 28、高さ 18 の tile を持つ。x=[0,10)、y=[0,10) は放射照度 E/pi の
// 1/5 乗（A は余弦で重みづけた空の見える割合）の 8x8 八面体、x=[10,28) は深度 moment（RG）と validity（B、tile 内で一定）の
// 16x16 八面体（どちらも境界 1 texel 込み）。深度は鋭い波瓣（約 10 度）に合わせて細かく取る。8x8（約 22 度間隔）だと
// 壁の縁を抜ける方向の深度が補間で混ざり、部屋の内角で壁の向こうの probe が半端に見えて鋸歯状に漏れる。
// 深度 tile の (10,0) の A は収束の重み（0..1）。(11 + 卦限, 1) texel の A は、その卦限の隣の 7 probe との間を形状が塞いでいないかの 7 bit（bit は軸の
// 差の組 x=1, y=2, z=4 から 1 を引いた位置）。
// x=[0,8), y=[10,18) は実測三角形に内接する円盤（oct 法線 xy、符号付き距離 z、半径 w）の 8x8 八面体。
// 空・alpha mask・退化三角形は半径ゼロ。幾何は補間せず、同じ角度の texel から読む。
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

// 実測三角形に内接する円盤との線分交差。三角形の外や alpha-mask の穴を勝手に塞がない。
// patch.xy は oct 法線、z は符号付き平面距離、w は円盤半径。長さは visibility scale で正規化済み。
bool HikariDynamicGiPatchVisible(float4 patch, float3 axis, float3 toReceiver, float tolerance)
{
    if (patch.w <= tolerance) return true;
    const float3 normal = HikariDecodeOctahedralDirection(patch.xy);
    const float cosine = dot(normal,toReceiver);
    const float axisCosine = dot(normal,axis);
    if (abs(cosine) < 1e-8f || abs(axisCosine) < 1e-5f) return true;
    const float t = patch.z / cosine;
    // 受光面自身との交差は遮蔽ではない。誤差は平面の垂直距離で扱う。光線に沿う距離は掠め角で発散する。
    if (t <= 0.0f || t >= 1.0f || abs(cosine-patch.z) <= tolerance) return true;
    const float3 center = axis * (patch.z/axisCosine);
    return length(toReceiver*t-center) >= patch.w-tolerance;
}

bool HikariDynamicGiProbePatchVisible(HikariIrradianceVolumeData volume, uint descriptorIndex,
    float2 tile, float3 toReceiver)
{
    const uint2 pixel = min((uint2)(HikariEncodeOctahedralDirection(toReceiver)*8.0f),7u.xx);
    const float3 axis = HikariDecodeOctahedralDirection((float2(pixel)+0.5f)/8.0f);
    const float4 patch = gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Load(
        int3(int2(tile)+int2(pixel.x,pixel.y+10u),0));
    return HikariDynamicGiPatchVisible(patch,axis,toReceiver/volume.selectionParams.w,
        max(0.002f/volume.selectionParams.w,0.002f));
}

#endif
