#ifndef HIKARI_POST_COLOR_GRADE_INCLUDED
#define HIKARI_POST_COLOR_GRADE_INCLUDED

// Saturation helpers used by stylized and cinematic post passes (Rec.601 luma).
// Contrast uses HikariAdjustContrast in Include/Lib/HIKARI_LibColor.hlsli.
#include "Include/Lib/HIKARI_LibColor.hlsli"

float HikariPostLuma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 HikariApplySaturation(float3 color, float saturation)
{
    return lerp(HikariPostLuma(color).xxx, color, saturation);
}

#endif
