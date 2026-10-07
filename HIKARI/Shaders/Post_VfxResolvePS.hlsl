#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
Texture2D<float4> gLayer : register(t0);
SamplerState gSampler : register(s0);

// PMA と加算成分を同時に再標本化する。非 jitter 出力への変換だけを行う。
float4 main(HikariPostEffectInput input) : SV_TARGET {
    float2 uv = input.uv + gUser[0].xy;
    if (any(uv < 0.0) || any(uv > 1.0)) return 0;
    return gLayer.SampleLevel(gSampler, uv, 0);
}
