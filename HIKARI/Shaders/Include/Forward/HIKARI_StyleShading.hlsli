#ifndef HIKARI_STYLE_SHADING_HLSLI
#define HIKARI_STYLE_SHADING_HLSLI

// 描画の様式が決める標準の材質の主光の受け方（LightCB の gStyleShading0/1）。定数からモデルの値を取り出し、
// HIKARI_StyleShadingModels.hlsli の本体を呼ぶ。詰め方は PackRenderStyleShading（C++）と対。
#include "Include/Forward/HIKARI_StyleShadingModels.hlsli"
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"

// 鏡面反射の強さはどのモデルでも掛ける。段数 0 は Physical（それ以外は変えない）。
void HikariApplyStyleShading(HikariPbrShadingContext context, float3 n, float3 l, float3 radiance,
                             float shadowVisibility, inout HikariDirectPbrLobes directional,
                             inout float3 ambientDiffuse)
{
    directional.specular *= gStyleShading0.w;
    if (gStyleShading0.x < 0.5f)
        return;
    HikariCelStyleParams params;
    params.bands = gStyleShading0.x;
    params.threshold = gStyleShading0.y;
    params.softness = gStyleShading0.z;
    params.shadowTint = gStyleShading1.rgb;
    params.rim = gStyleShading1.w;
    HikariShadeCelStyle(context, n, l, radiance, shadowVisibility, params, directional, ambientDiffuse);
}

#endif
