#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

float3 SampleScene(float2 uv)
{
    return gSceneTex.Sample(gSamp, saturate(uv)).rgb;
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = SampleScene(input.uv);
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
    {
        return float4(source, 1.0f);
    }

    const float curvature = max(gUser[0].y, 0.0f);
    const float scanlineStrength = saturate(gUser[0].z);
    const float phosphorStrength = saturate(gUser[0].w);
    const float chromaPixels = max(gUser[1].x, 0.0f);
    const float glitchStrength = saturate(gUser[1].y);
    const float vignetteStrength = saturate(gUser[1].z);
    const float rollingStrength = saturate(gUser[1].w);

    const float2 resolution = max(
        float2(gResolutionX, gResolutionY),
        float2(1.0f, 1.0f));
    const float2 texel = rcp(resolution);
    float2 centered = input.uv * 2.0f - 1.0f;
    const float radius2 = dot(centered, centered);
    centered *= 1.0f + curvature * radius2;
    const float2 uv = centered * 0.5f + 0.5f;
    const float inside = step(0.0f, uv.x) * step(uv.x, 1.0f) *
        step(0.0f, uv.y) * step(uv.y, 1.0f);

    // CYBER と同じ走査帯グリッチを、従来の粒状ノイズの代わりに使う。
    // CRT の走査線と蛍光体マスクは画面側に固定し、映像だけを横に裂く。
    const float strip = floor(uv.y * 54.0f + gTime * 3.0f);
    const float stripNoise = HikariHash12(float2(
        strip,
        floor(gTime * 12.0f)));
    const float glitchGate = step(0.86f, stripNoise) * glitchStrength;
    const float horizontalShift = (stripNoise - 0.5f) *
        glitchGate * 18.0f * texel.x;
    const float2 sampleUv = saturate(
        uv + float2(horizontalShift, 0.0f));

    const float glitchChromaPixels = chromaPixels *
        (1.0f + glitchGate * 3.0f);
    const float2 chromaOffset = float2(
        glitchChromaPixels * texel.x,
        0.0f);
    float3 color = float3(
        SampleScene(sampleUv + chromaOffset).r,
        SampleScene(sampleUv).g,
        SampleScene(sampleUv - chromaOffset).b);
    const float3 glow =
        SampleScene(sampleUv + texel * float2(1.5f, 0.0f)) +
        SampleScene(sampleUv - texel * float2(1.5f, 0.0f)) +
        SampleScene(sampleUv + texel * float2(0.0f, 1.5f)) +
        SampleScene(sampleUv - texel * float2(0.0f, 1.5f));
    color += max(glow * 0.25f - 0.62f.xxx, 0.0f.xxx) * 0.18f;

    const float pixelY = uv.y * resolution.y;
    const float scanline = lerp(
        1.0f,
        0.72f + 0.28f * sin(pixelY * 3.14159265f),
        scanlineStrength);
    color *= scanline;

    const uint triad = ((uint)floor(uv.x * resolution.x)) % 3u;
    const float3 phosphor = triad == 0u
        ? float3(1.10f, 0.82f, 0.82f)
        : (triad == 1u
            ? float3(0.82f, 1.10f, 0.82f)
            : float3(0.82f, 0.82f, 1.10f));
    color *= lerp(1.0f.xxx, phosphor, phosphorStrength);

    const float rollingBand = 0.5f + 0.5f * sin(
        uv.y * 8.0f - gTime * 1.8f);
    color *= 1.0f + (rollingBand - 0.5f) * rollingStrength * 0.10f;

    const float vignette = saturate(
        1.0f - radius2 * (0.34f + vignetteStrength * 0.62f));
    color *= vignette * inside;
    return float4(max(lerp(source, color, strength), 0.0f.xxx), 1.0f);
}
