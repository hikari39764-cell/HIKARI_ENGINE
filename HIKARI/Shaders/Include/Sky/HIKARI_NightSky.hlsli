#ifndef HIKARI_NIGHT_SKY_INCLUDED
#define HIKARI_NIGHT_SKY_INCLUDED
#include "../Color/HIKARI_Luminance.hlsli"

// 天体の光が大気で減る割合。天頂で約 0.2 等の減光、地平線へ向けて大気路長（エアマス）に比例して増える。
float HikariNightSkyExtinction(float elevationSine)
{
    return exp(-0.18f / max(elevationSine, 0.035f)) * smoothstep(-0.02f, 0.02f, elevationSine);
}

// 夜空の背景（大気光と分解できない星の光）。高度約 90 km の発光層を斜めに見るほど明るくなり（van Rhijn 効果）、
// 地平線付近では大気の減光で暗くなる。zenithLuminance は天頂の輝度 [nits]。
float3 HikariNightSkyBackground(float3 direction, float zenithLuminance)
{
    if (zenithLuminance <= 0.0f || direction.y <= -0.02f)
    {
        return 0.0f.xxx;
    }
    const float sinZenithSquared = saturate(1.0f - direction.y * direction.y);
    const float vanRhijn = rsqrt(max(1.0f - 0.9724f * sinZenithSquared, 1.0e-3f));
    // 天頂の減光で割り、天頂の値を zenithLuminance に保つ。わずかに青みのある中性色（輝度 1）。
    const float3 color = float3(0.86f, 0.95f, 1.0f) / HikariLuminance(float3(0.86f, 0.95f, 1.0f));
    return color * (zenithLuminance * vanRhijn * HikariNightSkyExtinction(direction.y) / HikariNightSkyExtinction(1.0f));
}

#endif
