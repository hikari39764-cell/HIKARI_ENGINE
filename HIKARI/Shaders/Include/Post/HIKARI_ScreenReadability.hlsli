#ifndef HIKARI_SCREEN_READABILITY_INCLUDED
#define HIKARI_SCREEN_READABILITY_INCLUDED

cbuffer HikariScreenReadabilityConstants : register(b1)
{
    uint gReadabilityCount;
    uint gReadabilityAttenuateCopy;
    float gReadabilityCurvature;
    float gReadabilityPadding;
    float4 gReadabilityCorners[8];
};

float HikariScreenReadability(float2 uv, float2 resolution)
{
    float result = 0.0f;
    const float2 pixel = uv * resolution;
    [loop]
    for (uint region = 0u; region < min(gReadabilityCount, 4u); ++region)
    {
        const float4 a = gReadabilityCorners[region * 2u];
        const float4 b = gReadabilityCorners[region * 2u + 1u];
        const float2 minimum = min(min(a.xy, a.zw), min(b.xy, b.zw));
        const float2 maximum = max(max(a.xy, a.zw), max(b.xy, b.zw));
        if (any(uv < minimum) || any(uv > maximum)) continue;
        const float2 corners[4] = {a.xy * resolution, a.zw * resolution, b.xy * resolution, b.zw * resolution};
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

float HikariScreenReadabilityAfterWarp(float2 uv, float2 resolution)
{
    float2 centered = uv * 2.0f - 1.0f;
    centered *= 1.0f + gReadabilityCurvature * dot(centered, centered);
    return HikariScreenReadability(centered * .5f + .5f, resolution);
}

#endif
