#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

static const float HIKARI_BLOOM_MAX_SAFE_RADIANCE = 4096.0f;

float3 HikariSanitizeBloomRadiance(float3 color)
{
    if (!all(isfinite(color)) || any(color >= 65504.0f.xxx))
    {
        return 0.0f.xxx;
    }

    color = max(color, 0.0f.xxx);
    float maximumChannel = max(max(color.r, color.g), color.b);
    if (maximumChannel > HIKARI_BLOOM_MAX_SAFE_RADIANCE)
    {
        color *= HIKARI_BLOOM_MAX_SAFE_RADIANCE / maximumChannel;
    }
    return color;
}

float3 HikariSampleBloomInput(float2 uv)
{
    return HikariSanitizeBloomRadiance(
        gSceneTex.SampleLevel(gSamp, uv, 0.0f).rgb);
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    // 1/4 解像度の各出力画素が覆う 4x4 領域を四つの bilinear tap で積分する。
    // 中央だけ読むと細い高光を移動位相によって取りこぼす。重みの総和は 1。
    const float2 offset = 0.25f * gUser[1].xy;
    float3 color = 0.25f * (
        HikariSampleBloomInput(input.uv + float2(-offset.x, -offset.y)) +
        HikariSampleBloomInput(input.uv + float2( offset.x, -offset.y)) +
        HikariSampleBloomInput(input.uv + float2(-offset.x,  offset.y)) +
        HikariSampleBloomInput(input.uv + float2( offset.x,  offset.y)));
    float threshold = max(gUser[0].x, 0.0f);
    float intensity = max(gUser[0].y, 0.0f);
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float softKnee = max(threshold * 0.5f, 0.0001f);
    float softContribution = clamp(
        luminance - threshold + softKnee,
        0.0f,
        softKnee * 2.0f);
    softContribution =
        softContribution * softContribution /
        (softKnee * 4.0f + 0.0001f);
    float contribution = max(
        luminance - threshold,
        softContribution);
    float bloomWeight =
        contribution / max(luminance, 0.0001f);
    float3 bloom = HikariSanitizeBloomRadiance(
        color * bloomWeight * intensity);
    return float4(bloom, 0.0f);
}
