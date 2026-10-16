#ifndef HIKARI_SCREEN_READABILITY_INCLUDED
#define HIKARI_SCREEN_READABILITY_INCLUDED

cbuffer HikariScreenReadabilityConstants : register(b1)
{
    uint gReadabilityCount;
    uint gReadabilityAttenuateCopy;
    float gReadabilityCurvature;
    float gReadabilityPadding;
    // 領域ごとの四隅の画面座標（uv）。各隅の x, y を [-4, 5] の 16 bit 正規化で 1 語に詰める（C++ の
    // PackScreenReadabilityCorner と同じ）。root constant の 64 語に 12 領域を収めるため。
    uint4 gReadabilityCorners[12];
    // 領域ごとの画面の一番手前の隅の 1 - 深度（16 bit 正規化、2 領域で 1 語）。
    uint4 gReadabilityNearDepth[2];
};

float HikariReadabilityNearInverseDepth(uint region)
{
    const uint packed = gReadabilityNearDepth[region / 8u][(region / 2u) % 4u];
    return float((packed >> ((region & 1u) * 16u)) & 0xFFFFu) / 65535.0f;
}

float2 HikariUnpackReadabilityCorner(uint packed)
{
    return float2(packed & 0xFFFFu, packed >> 16u) * (9.0f / 65535.0f) - 4.0f;
}

// sceneDepth はその画素の不透明な面の深度（0 = 近い面, 1 = 遠い面・空）。画面の一番手前の隅より 1.5% 以上手前の面は
// 画面の前にある物（他の電視など）なので保護しない。深度の無い呼び手は 0 を渡し、遮りを調べない（従来どおり）。
float HikariScreenReadabilityAt(float2 uv, float2 resolution, float sceneDepth)
{
    const float inverseSceneDepth = sceneDepth > 0.0f ? 1.0f - sceneDepth : -1.0f;
    float result = 0.0f;
    const float2 pixel = uv * resolution;
    [loop]
    for (uint region = 0u; region < min(gReadabilityCount, 12u); ++region)
    {
        const uint4 packed = gReadabilityCorners[region];
        const float2 c0 = HikariUnpackReadabilityCorner(packed.x);
        const float2 c1 = HikariUnpackReadabilityCorner(packed.y);
        const float2 c2 = HikariUnpackReadabilityCorner(packed.z);
        const float2 c3 = HikariUnpackReadabilityCorner(packed.w);
        const float2 minimum = min(min(c0, c1), min(c2, c3));
        const float2 maximum = max(max(c0, c1), max(c2, c3));
        if (any(uv < minimum) || any(uv > maximum)) continue;
        if (inverseSceneDepth >= 0.0f && inverseSceneDepth > HikariReadabilityNearInverseDepth(region) * 1.015f + 1.0e-5f)
            continue;
        const float2 corners[4] = {c0 * resolution, c1 * resolution, c2 * resolution, c3 * resolution};
        float minEdge = 1.0e8f;
        float maxEdge = -1.0e8f;
        [unroll]
        for (uint edge = 0u; edge < 4u; ++edge)
        {
            const float2 direction = corners[(edge + 1u) % 4u] - corners[edge];
            const float2 delta = pixel - corners[edge];
            const float signedDistance = (direction.x * delta.y - direction.y * delta.x) / max(length(direction), .0001f);
            minEdge = min(minEdge, signedDistance);
            maxEdge = max(maxEdge, signedDistance);
        }
        result = max(result, smoothstep(0.0f, 1.5f, max(minEdge, -maxEdge)));
    }
    return result;
}

float HikariScreenReadability(float2 uv, float2 resolution)
{
    return HikariScreenReadabilityAt(uv, resolution, 0.0f);
}

float HikariScreenReadabilityAfterWarp(float2 uv, float2 resolution)
{
    float2 centered = uv * 2.0f - 1.0f;
    centered *= 1.0f + gReadabilityCurvature * dot(centered, centered);
    return HikariScreenReadability(centered * .5f + .5f, resolution);
}

#endif
