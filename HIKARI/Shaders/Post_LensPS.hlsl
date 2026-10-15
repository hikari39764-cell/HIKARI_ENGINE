#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

// 撮影レンズの癖（露出済みのシーン線形 HDR に掛ける。ブルームの前なので明るい縁の色ずれも光る）。
// 周辺減光: 自然な cos^4 に近い 1/(1+a r^2)^2 の落ち。r は画面の隅で 1、強さは隅での減光の割合。
// 色収差: 中心からの距離の二乗に比例して赤を内へ、青を外へずらす。量は画面の隅でのずれ [px]。
// 粒子: 輝度に掛ける乗算の揺らぎ（フィルムの濃度の揺らぎに近く、明部で目立ちすぎない）。粒の大きさ [px]。
float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float vignette = saturate(gUser[0].x);
    const float roundness = saturate(gUser[0].y);
    const float aberration = max(gUser[0].z, 0.0f);
    const float grain = saturate(gUser[1].x);
    const float grainSize = max(gUser[1].y, 1.0f);
    const float grainShadowBias = saturate(gUser[1].z);

    const float2 resolution = max(float2(gResolutionX, gResolutionY), 1.0f.xx);
    const float2 centered = input.uv * 2.0f - 1.0f;
    const float4 scene = gSceneTex.SampleLevel(gSamp, input.uv, 0.0f);
    float3 color = scene.rgb;
    if (aberration > 0.0f)
    {
        // 隅（|centered| = sqrt(2)）で aberration 画素。
        const float2 shift = centered * dot(centered, centered) * (0.25f * aberration / resolution);
        color.r = gSceneTex.SampleLevel(gSamp, input.uv - shift, 0.0f).r;
        color.b = gSceneTex.SampleLevel(gSamp, input.uv + shift, 0.0f).b;
    }

    if (vignette > 0.0f)
    {
        // roundness 1 で画面の縦横比に依らない円、0 で画面の形に沿う楕円。
        const float aspect = resolution.x / resolution.y;
        const float2 axis = float2(lerp(1.0f, aspect, roundness), 1.0f);
        const float r2 = dot(centered * axis, centered * axis) / dot(axis, axis);
        const float a = rsqrt(max(1.0f - vignette, 1.0e-3f)) - 1.0f;
        const float falloff = 1.0f / (1.0f + a * r2);
        color *= falloff * falloff;
    }

    if (grain > 0.0f)
    {
        // 粒ごと・フレームごとの三角分布の揺らぎ。暗部寄りの重みは露出済みの輝度で決める。
        const float2 cell = floor(input.position.xy / grainSize);
        const float frame = floor(gTime * 60.0f);
        const float noise = HikariHash12(cell + frame * float2(17.13f, 31.71f)) +
            HikariHash12(cell.yx + frame * float2(5.37f, 11.93f)) - 1.0f;
        const float luminance = HikariLuminance(color);
        const float response = lerp(1.0f, 1.0f - luminance / (luminance + 0.25f), grainShadowBias);
        color *= max(1.0f + grain * response * noise, 0.0f);
    }
    return float4(max(color, 0.0f), scene.a);
}
