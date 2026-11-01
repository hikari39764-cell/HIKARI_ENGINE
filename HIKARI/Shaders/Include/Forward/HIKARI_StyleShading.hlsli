#ifndef HIKARI_STYLE_SHADING_HLSLI
#define HIKARI_STYLE_SHADING_HLSLI

// 描画の様式が決める標準の材質の主光の受け方。LightCB の gStyleShading0/1 を
// HIKARI_StyleShadingModels.hlsli の HikariApplyPackedStyleShading へ渡す。
#include "Include/Forward/HIKARI_StyleShadingModels.hlsli"
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"

void HikariApplyStyleShading(HikariPbrShadingContext context, float3 n, float3 l, float3 radiance,
                             float shadowVisibility, inout HikariDirectPbrLobes directional,
                             inout float3 ambientDiffuse)
{
    HikariApplyPackedStyleShading(gStyleShading0, gStyleShading1, context, n, l, radiance, shadowVisibility,
                                  directional, ambientDiffuse);
}

#endif
