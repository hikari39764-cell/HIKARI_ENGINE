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
    } else if (kind == 12) {
        // 中間の灰（0.18）を軸に HDR のまま反転する。暗部は明るく、光る所は暗く沈む。
        float3 inverted = min(0.0324 / max(color, 1.0e-3), 8.0);
        color = lerp(color, inverted, saturate(amount));
    } else if (kind == 13) {
        // 画面中心へ向かって 16 点を平均する。画素ごとにずらして縞を目立たせない。
        float2 toCenter = 0.5 - uv;
        float stretch = saturate(amount);
        float jitter = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
        float3 sum = 0.0;
        [unroll] for (int i = 0; i < 16; ++i)
            sum += gTex.Sample(gSamp, saturate(uv + toCenter * stretch * ((i + jitter) / 16.0))).rgb;
        color = sum / 16.0;
    }
    // HDR を保持する。露出・トーンマッピングは後段の唯一の resolve が所有。
    return float4(max(lerp(source.rgb, color, weight), 0.0), source.a);
}
