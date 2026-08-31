#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

#include "Include/Post/HIKARI_PostHash.hlsli"
#include "Include/Post/HIKARI_PostColorGrade.hlsli"

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    float2 uv = input.uv;

    float vignette     = gUser[0].x;
    float chroma       = gUser[0].y;
    float saturation   = gUser[0].z;
    float contrast     = gUser[0].w;

    float gain         = gUser[1].x;
    float grainAmount  = gUser[1].y;
    float scanlineAmp  = gUser[1].z;
    float scanlineFreq = gUser[1].w;

    float2 ca = float2(chroma, 0.0);
    float r = gTex.Sample(gSamp, uv + ca).r;
    float g = gTex.Sample(gSamp, uv).g;
    float b = gTex.Sample(gSamp, uv - ca).b;

    float3 color = float3(r, g, b);

    color = HikariApplySaturation(color, saturation);
    color = HikariApplyContrast(color, contrast);
    color *= gain;

    float2 center = uv * 2.0 - 1.0;
    float vig = 1.0 - dot(center, center) * vignette;
    color *= saturate(vig);

    float scanline = 1.0 +
        sin(uv.y * scanlineFreq + gTime * 1.5) * scanlineAmp;
    color *= scanline;

    float grain = HikariHash12(
        uv * float2(gResolutionX, gResolutionY) + gTime * 31.7) - 0.5;
    color += grain * grainAmount;

    return float4(saturate(color), 1.0);
}
