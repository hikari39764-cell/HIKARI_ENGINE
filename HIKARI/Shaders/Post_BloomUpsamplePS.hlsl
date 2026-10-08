#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

// Bloom のピラミッドを一段戻す。一つ小さい段を 3x3 のテントで広げ、加算合成でこの段へ足す。
// 小さい段ほど広くぼけた光になり、全段の和が太陽や強い反射のまわりのなめらかに減衰する光の輪になる。
// gUser[0]: x 広がりの倍率（入力の texel 単位）
// gUser[1]: xy 入力の texel
Texture2D gSourceTex : register(t0);
SamplerState gSamp : register(s0);

float3 SampleSource(float2 uv)
{
    const float3 color = gSourceTex.SampleLevel(gSamp, uv, 0.0f).rgb;
    return all(isfinite(color)) ? max(color, 0.0f.xxx) : 0.0f.xxx;
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float2 d = gUser[1].xy * max(gUser[0].x, 0.0f);
    const float2 uv = input.uv;
    float3 sum = SampleSource(uv) * 4.0f;
    sum += (SampleSource(uv + float2(-d.x, 0.0f)) + SampleSource(uv + float2(d.x, 0.0f)) +
               SampleSource(uv + float2(0.0f, -d.y)) + SampleSource(uv + float2(0.0f, d.y))) * 2.0f;
    sum += SampleSource(uv + float2(-d.x, -d.y)) + SampleSource(uv + float2(d.x, -d.y)) +
           SampleSource(uv + float2(-d.x, d.y)) + SampleSource(uv + float2(d.x, d.y));
    return float4(sum * (1.0f / 16.0f), 0.0f);
}
