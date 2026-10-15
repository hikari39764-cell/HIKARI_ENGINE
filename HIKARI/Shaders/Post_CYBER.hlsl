#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

float DisplayLuma(float3 color)
{
    const float value = max(HikariLuminance(color), 0.0f);
    return value / (1.0f + value);
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
    {
        return float4(source, 1.0f);
    }

    const float edgeGlow = max(gUser[0].y, 0.0f);
    const float glitchStrength = saturate(gUser[0].z);
    const float scanlineStrength = saturate(gUser[0].w);
    const float3 shadowColor = max(gUser[1].rgb, 0.0f.xxx);
    const float3 midColor = max(gUser[2].rgb, 0.0f.xxx);
    const float3 highlightColor = max(gUser[3].rgb, 0.0f.xxx);
    const float lowThreshold = saturate(gUser[4].x);
    const float highThreshold = max(saturate(gUser[4].y), lowThreshold);
    const float chromaPixels = max(gUser[4].z, 0.0f);
    const float vignetteStrength = saturate(gUser[4].w);
    const float2 resolution = max(
        float2(gResolutionX, gResolutionY),
        float2(1.0f, 1.0f));
    const float2 texel = rcp(resolution);

    const float strip = floor(input.uv.y * 54.0f + gTime * 3.0f);
    const float stripNoise = HikariHash12(float2(strip, floor(gTime * 12.0f)));
    const float glitchGate = step(0.86f, stripNoise) * glitchStrength;
    const float horizontalShift = (stripNoise - 0.5f) *
        glitchGate * 18.0f * texel.x;
    const float2 uv = saturate(input.uv + float2(horizontalShift, 0.0f));
    const float2 chroma = float2(chromaPixels * texel.x, 0.0f);
    const float3 sampled = float3(
        gSceneTex.Sample(gSamp, saturate(uv + chroma)).r,
        gSceneTex.Sample(gSamp, uv).g,
        gSceneTex.Sample(gSamp, saturate(uv - chroma)).b);

    const float display = DisplayLuma(sampled);
    float3 paletteColor = display < lowThreshold
        ? shadowColor
        : (display < highThreshold ? midColor : highlightColor);
    const float tierEnergy = display < lowThreshold
        ? 0.58f
        : (display < highThreshold ? 1.0f : 1.42f);
    paletteColor *= tierEnergy;

    const float lN = DisplayLuma(gSceneTex.Sample(
        gSamp, uv + float2(0.0f, -texel.y)).rgb);
    const float lS = DisplayLuma(gSceneTex.Sample(
        gSamp, uv + float2(0.0f, texel.y)).rgb);
    const float lW = DisplayLuma(gSceneTex.Sample(
        gSamp, uv + float2(-texel.x, 0.0f)).rgb);
    const float lE = DisplayLuma(gSceneTex.Sample(
        gSamp, uv + float2(texel.x, 0.0f)).rgb);
    const float edge = saturate(abs(lE - lW) + abs(lS - lN));
    paletteColor += highlightColor * edge * edgeGlow;

    const float scan = 0.86f + 0.14f * sin(
        uv.y * resolution.y * 3.14159265f);
    paletteColor *= lerp(1.0f, scan, scanlineStrength);
    const float2 centered = input.uv * 2.0f - 1.0f;
    paletteColor *= saturate(
        1.0f - dot(centered, centered) * vignetteStrength * 0.42f);
    return float4(max(lerp(source, paletteColor, strength), 0.0f.xxx), 1.0f);
}
