#ifndef HIKARI_SHADOW_PCF_INCLUDED
#define HIKARI_SHADOW_PCF_INCLUDED

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

#endif
