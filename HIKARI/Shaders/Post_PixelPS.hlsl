// 像素風。表示の色の段階で画面を大きな画素の格子に揃え、各画素は格子の中心の色を取る。色は段数に量子化し、
// Bayer 行列で揺らして段の境を点描にする。幾何の線は PreTemporal の共通 pass で既に色へ含まれている。
#include "Include/Post/HIKARI_PostStylize.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

float3 AdjustColor(float3 color, float saturation, float contrast)
{
    const float luma = HikariLuminance(color);
    color = lerp(luma.xxx, color, saturation);
    return saturate((color - 0.5f) * contrast + 0.5f);
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
        return float4(source, 1.0f);

    const float scale = HikariStylizePixelScale();
    const float blockPixels = max(round(gUser[0].y * scale), 1.0f);
    const float levels = clamp(round(gUser[0].z), 2.0f, 64.0f);
    const float dither = saturate(gUser[0].w);
    const float saturation = max(gUser[1].y, 0.0f);
    const float contrast = max(gUser[1].z, 0.05f);

    const float2 resolution = max(float2(gResolutionX, gResolutionY), 1.0f.xx);
    const float2 block = floor(input.position.xy / blockPixels);
    const float2 blockUv = (block + 0.5f) * blockPixels / resolution;
    float3 color = AdjustColor(gSceneTex.SampleLevel(gSamp, blockUv, 0.0f).rgb, saturation, contrast);

    // 段の量子化。揺らしは格子の画素単位で掛け、大きな画素の中は一色に保つ。
    const float threshold = HikariStylizeBayer4(uint2(block)) * dither;
    color = saturate(floor(color * (levels - 1.0f) + 0.5f + threshold) / (levels - 1.0f));

    return float4(lerp(source, color, strength), 1.0f);
}
