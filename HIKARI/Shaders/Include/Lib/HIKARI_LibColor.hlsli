#ifndef HIKARI_LIB_COLOR_HLSLI
#define HIKARI_LIB_COLOR_HLSLI

// 色の変換と合成。色は線形 RGB で受け渡す（sRGB の値を扱うときは先に HikariSrgbToLinear）。
#include "Include/Color/HIKARI_Luminance.hlsli"

float3 HikariSrgbToLinear(float3 c)
{
    return select(c <= 0.04045f, c / 12.92f, pow(max((c + 0.055f) / 1.055f, 0.0f), 2.4f));
}

float3 HikariLinearToSrgb(float3 c)
{
    return select(c <= 0.0031308f, c * 12.92f, 1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

// 色相・彩度・明度（すべて 0..1、色相は一周 1）。
float3 HikariRgbToHsv(float3 c)
{
    const float4 k = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    const float4 p = c.g < c.b ? float4(c.bg, k.wz) : float4(c.gb, k.xy);
    const float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    const float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + 1.0e-10f)), d / (q.x + 1.0e-10f), q.x);
}

float3 HikariHsvToRgb(float3 hsv)
{
    const float3 p = abs(frac(hsv.xxx + float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f);
    return hsv.z * lerp(1.0f.xxx, saturate(p - 1.0f), hsv.y);
}

// 彩度（0 で灰、1 でそのまま、1 より大きいと強める）。灰は Rec.709 の輝度。
float3 HikariAdjustSaturation(float3 c, float saturation)
{
    return lerp(HikariLuminance(c).xxx, c, saturation);
}

// pivot を中心にコントラストを変える（線形の場面色なら pivot は 0.18 付近）。
float3 HikariAdjustContrast(float3 c, float contrast, float pivot)
{
    return (c - pivot) * contrast + pivot;
}

// 合成（base が下、blend が上。0..1 の色に使う）。
float3 HikariBlendMultiply(float3 base, float3 blend) { return base * blend; }
float3 HikariBlendScreen(float3 base, float3 blend) { return 1.0f - (1.0f - base) * (1.0f - blend); }
float3 HikariBlendOverlay(float3 base, float3 blend)
{
    return select(base < 0.5f, 2.0f * base * blend, 1.0f - 2.0f * (1.0f - base) * (1.0f - blend));
}
float3 HikariBlendSoftLight(float3 base, float3 blend)
{
    return select(blend < 0.5f, base - (1.0f - 2.0f * blend) * base * (1.0f - base),
                  base + (2.0f * blend - 1.0f) * (sqrt(base) - base));
}

#endif
