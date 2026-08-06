#ifndef HIKARI_POST_COLOR_GRADE_INCLUDED
#define HIKARI_POST_COLOR_GRADE_INCLUDED

// Shared saturation/contrast helpers used by stylized and cinematic post
// passes. One implementation instead of per-pass copies.
float HikariPostLuma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 HikariApplySaturation(float3 color, float saturation)
{
    return lerp(HikariPostLuma(color).xxx, color, saturation);
}

float3 HikariApplyContrast(float3 color, float contrast)
{
    return (color - 0.5f.xxx) * contrast + 0.5f.xxx;
}

#endif
