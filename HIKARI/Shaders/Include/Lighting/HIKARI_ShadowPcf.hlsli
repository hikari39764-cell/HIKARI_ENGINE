#ifndef HIKARI_SHADOW_PCF_INCLUDED
#define HIKARI_SHADOW_PCF_INCLUDED

#include "Include/Post/HIKARI_PostHash.hlsli"

// 5x5 の線形比較 PCF。単位半径では同じ 6x6 texel の重みを 3x3 回にまとめる。
// atlas 境界と任意半径は従来の各 tap clamp を保持する。
float HikariShadowPcf5x5(Texture2D<float> depth, SamplerComparisonState comparison,
    float2 uv, float receiverDepth, float2 texelSize, float2 clampMin, float2 clampMax, float radius)
{
    if (radius <= 0.00001f)
        return depth.SampleCmpLevelZero(comparison, clamp(uv, clampMin, clampMax), receiverDepth);

    [branch]
    if (radius == 1.0f && all(uv - 2.0f * texelSize >= clampMin) &&
        all(uv + 2.0f * texelSize <= clampMax))
    {
        const float2 position = uv / texelSize - 0.5f;
        const float2 fraction = frac(position);
        const float2 base = (floor(position) + 0.5f) * texelSize;
        const float3 weightsX = float3(2.0f - fraction.x, 2.0f, 1.0f + fraction.x);
        const float3 weightsY = float3(2.0f - fraction.y, 2.0f, 1.0f + fraction.y);
        const float3 offsetsX = float3(-2.0f + rcp(weightsX.x), 0.5f, 2.0f + fraction.x / weightsX.z);
        const float3 offsetsY = float3(-2.0f + rcp(weightsY.x), 0.5f, 2.0f + fraction.y / weightsY.z);
        float sum = 0.0f;
        [unroll] for (uint y = 0u; y < 3u; ++y)
            [unroll] for (uint x = 0u; x < 3u; ++x)
                sum += weightsX[x] * weightsY[y] * depth.SampleCmpLevelZero(comparison,
                    base + float2(offsetsX[x], offsetsY[y]) * texelSize, receiverDepth);
        return sum * (1.0f / 25.0f);
    }

    float sum = 0.0f;
    [unroll] for (int y = -2; y <= 2; ++y)
        [unroll] for (int x = -2; x <= 2; ++x)
            sum += depth.SampleCmpLevelZero(comparison,
                clamp(uv + float2(x, y) * texelSize * radius, clampMin, clampMax), receiverDepth);
    return sum * (1.0f / 25.0f);
}

// Vogel 円盤の tap。黄金角で並び、回転角で tap の向きを変える。
float2 HikariShadowVogelTap(uint index, uint count, float rotation)
{
    const float radius = sqrt((float(index) + 0.5f) / float(count));
    const float angle = float(index) * 2.39996322973f + rotation;
    return float2(cos(angle), sin(angle)) * radius;
}

// PCSS：遮蔽者の平均深度から半影の幅を決め、その幅で比較フィルタする（遮蔽者が遠いほど柔らかい）。
// depthToTexels は影の深度 1（near→far）が受け手で何 texel の半影になるか。遮蔽者探索は Load で読むので
// sampler を増やさない。rotation は画素とフレームで変え、TAA が均す。
static const uint HIKARI_SHADOW_PCSS_SEARCH_TAPS = 12u;
static const uint HIKARI_SHADOW_PCSS_FILTER_TAPS = 16u;

float HikariShadowPcss(Texture2D<float> depth, SamplerComparisonState comparison, float2 uv, float receiverDepth,
    float2 texelSize, float2 clampMin, float2 clampMax, float depthToTexels, float maxTexels, float rotation)
{
    const float2 atlasSize = rcp(texelSize);
    float blockerSum = 0.0f;
    uint blockerCount = 0u;
    [unroll] for (uint search = 0u; search < HIKARI_SHADOW_PCSS_SEARCH_TAPS; ++search)
    {
        const float2 tapUv = clamp(uv + HikariShadowVogelTap(search, HIKARI_SHADOW_PCSS_SEARCH_TAPS, rotation) *
            (maxTexels * texelSize), clampMin, clampMax);
        const float blocker = depth.Load(int3((int2)(tapUv * atlasSize), 0));
        if (blocker < receiverDepth)
        {
            blockerSum += blocker;
            ++blockerCount;
        }
    }
    if (blockerCount == 0u)
        return 1.0f;
    // 下限は従来の PCF と同じ程度のぼかし。影マップの texel の階段を見せない。
    const float penumbraTexels =
        clamp((receiverDepth - blockerSum / float(blockerCount)) * depthToTexels, 1.5f, maxTexels);
    float sum = 0.0f;
    [unroll] for (uint tap = 0u; tap < HIKARI_SHADOW_PCSS_FILTER_TAPS; ++tap)
    {
        const float2 tapUv = clamp(uv + HikariShadowVogelTap(tap, HIKARI_SHADOW_PCSS_FILTER_TAPS, rotation + 1.0f) *
            (penumbraTexels * texelSize), clampMin, clampMax);
        sum += depth.SampleCmpLevelZero(comparison, tapUv, receiverDepth);
    }
    return sum / float(HIKARI_SHADOW_PCSS_FILTER_TAPS);
}

#endif
