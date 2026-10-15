#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"
Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    float2 uv = input.uv;
    uint kind = (uint)gUser[0].x;
    float amount = gUser[0].y;
    float weight = saturate(gUser[0].z);
    float3 tint = max(gUser[1].rgb, 0.0);
    float4 source = gTex.Sample(gSamp, uv);
    float3 color = source.rgb;
    if (kind == 6) {
        float2 p = uv * 2.0 - 1.0;
        color *= 1.0 - saturate(dot(p, p) * max(amount, 0.0));
    } else if (kind == 7) {
        color = lerp(color, HikariLuminance(color).xxx, saturate(amount));
    } else if (kind == 8) {
        color *= lerp(1.0.xxx, tint, saturate(amount));
    } else if (kind == 9) {
        color = lerp(color, tint, saturate(amount));
    } else if (kind == 10) {
        float2 size = max(float2(gResolutionX, gResolutionY), 1.0);
        float pixels = max(1.0, amount);
        float2 sampleUv = (floor(uv * size / pixels) + 0.5) * pixels / size;
        color = gTex.Sample(gSamp, saturate(sampleUv)).rgb;
    } else if (kind == 11) {
        float2 shift = (uv - 0.5) * amount / max(float2(gResolutionX, gResolutionY), 1.0);
        color.r = gTex.Sample(gSamp, saturate(uv + shift)).r;
        color.b = gTex.Sample(gSamp, saturate(uv - shift)).b;
    }
    // HDR を保持する。露出・トーンマッピングは後段の唯一の resolve が所有。
    return float4(max(lerp(source.rgb, color, weight), 0.0), source.a);
}
