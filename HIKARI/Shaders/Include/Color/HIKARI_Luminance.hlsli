#ifndef HIKARI_LUMINANCE_HLSLI
#define HIKARI_LUMINANCE_HLSLI

// 線形 Rec.709 の相対輝度。場面色・露出・時間方向の比較・画面効果が共有する（FXAA の知覚輝度は別の係数）。
float HikariLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

#endif
