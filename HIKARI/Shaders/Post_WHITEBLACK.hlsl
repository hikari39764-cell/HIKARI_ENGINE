#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

float Luma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float DisplayLuma(float3 color)
{
    const float value = max(Luma(color), 0.0f);
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

    const float inkStrength = saturate(gUser[0].y);
    const float toneLevels = max(gUser[0].z, 2.0f);
    const float halftoneStrength = saturate(gUser[0].w);
    const float contrast = max(gUser[1].x, 0.05f);
    const float edgeThreshold = max(gUser[1].y, 0.001f);
    const float paperGrain = saturate(gUser[1].z);
    const float dotScale = max(gUser[1].w, 2.0f);
    const float2 resolution = max(
        float2(gResolutionX, gResolutionY),
        float2(1.0f, 1.0f));
    const float2 texel = rcp(resolution);

    const float lN = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(0.0f, -texel.y)).rgb);
    const float lS = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(0.0f, texel.y)).rgb);
    const float lW = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(-texel.x, 0.0f)).rgb);
    const float lE = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(texel.x, 0.0f)).rgb);
    const float lNW = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv - texel).rgb);
    const float lNE = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(texel.x, -texel.y)).rgb);
    const float lSW = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + float2(-texel.x, texel.y)).rgb);
    const float lSE = DisplayLuma(gSceneTex.Sample(
        gSamp, input.uv + texel).rgb);
    const float sobelX = (lNE + 2.0f * lE + lSE) -
        (lNW + 2.0f * lW + lSW);
    const float sobelY = (lSW + 2.0f * lS + lSE) -
        (lNW + 2.0f * lN + lNE);
    const float edge = smoothstep(
        edgeThreshold,
        edgeThreshold * 2.7f,
        length(float2(sobelX, sobelY)));

    float display = DisplayLuma(source);
    display = saturate((display - 0.5f) * contrast + 0.5f);
    display = floor(display * toneLevels + 0.5f) / toneLevels;
    const float2 dotUv = input.uv * resolution / dotScale;
    const float2 dotCell = frac(dotUv) - 0.5f;
    const float dotRadius = lerp(0.12f, 0.48f, 1.0f - display);
    const float dotMask = 1.0f - smoothstep(
        dotRadius,
        dotRadius + 0.08f,
        length(dotCell));
    display *= lerp(1.0f, 0.74f + 0.26f * dotMask, halftoneStrength);
    display *= 1.0f - edge * inkStrength;

    const float grain = HikariHash12(
        floor(input.uv * resolution * 0.5f) + 19.0f) - 0.5f;
    display = saturate(display + grain * paperGrain * 0.08f);
    const float hdrLuma = display / max(1.0f - display, 0.035f);
    const float3 manga = hdrLuma.xxx;
    return float4(max(lerp(source, manga, strength), 0.0f.xxx), 1.0f);
}
