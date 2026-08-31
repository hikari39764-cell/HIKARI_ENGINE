#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

// Scene lighting stays linear HDR through the render pipeline. This pass
// applies the selected tone curve and the single display gamma transform.

float3 ApplyReinhard(float3 color)
{
    return color / (color + 1.0f);
}

float3 ApplyACESFitted(float3 color)
{
    const float3x3 inputMatrix = float3x3(
        0.59719f, 0.35458f, 0.04823f,
        0.07600f, 0.90834f, 0.01566f,
        0.02840f, 0.13383f, 0.83777f);
    const float3x3 outputMatrix = float3x3(
         1.60475f, -0.53108f, -0.07367f,
        -0.10208f,  1.10813f, -0.00605f,
        -0.00327f, -0.07276f,  1.07602f);

    float3 aces = mul(inputMatrix, max(color, 0.0f));
    const float3 numerator =
        aces * (aces + 0.0245786f) - 0.000090537f;
    const float3 denominator =
        aces * (0.983729f * aces + 0.4329510f) + 0.238081f;
    aces = numerator / max(denominator, 1e-5f);
    return saturate(mul(outputMatrix, aces));
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    float4 sceneColor = gSceneTex.Sample(gSamp, input.uv);
    const bool toneCurveEnabled = gUser[0].x >= 0.5f;
    const float exposure = max(gUser[0].y, 0.0f);
    const float gamma = max(gUser[0].z, 0.01f);
    const int mode = (int)round(gUser[0].w);
    const float3 whiteBalance = max(gUser[1].rgb, 0.0f);
    const float contrast = clamp(gUser[1].a, 0.25f, 2.0f);
    const float saturation = clamp(gUser[2].x, 0.0f, 2.0f);

    float3 color = max(sceneColor.rgb, 0.0f) * exposure * whiteBalance;
    const float luminance = dot(
        color,
        float3(0.2126f, 0.7152f, 0.0722f));
    color = max(
        lerp(luminance.xxx, color, saturation),
        0.0f);
    if (toneCurveEnabled)
    {
        if (mode == 1)
        {
            color = ApplyReinhard(color);
        }
        else if (mode == 2)
        {
            color = ApplyACESFitted(color);
        }
    }

    color = max((color - 0.18f) * contrast + 0.18f, 0.0f);
    color = pow(saturate(color), 1.0f / gamma);
    return float4(color, sceneColor.a);
}
