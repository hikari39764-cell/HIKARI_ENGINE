#ifndef HIKARI_STYLE_SHADING_HLSLI
#define HIKARI_STYLE_SHADING_HLSLI

// 描画の様式が決める標準の材質の主光の受け方（LightCB の gStyleShading0/1）。物理（段数 0）なら何もしない。
#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"

// 主光の当たり lit（余弦 × 影）を、影と明るい側 bands 段に分けた明るさ（0..1）。
float HikariStyleCelBand(float lit, float bands, float threshold, float softness)
{
    float band = 0.0f;
    [loop]
    for (float level = 0.0f; level < bands; level += 1.0f)
    {
        const float edge = threshold + (1.0f - threshold) * level / bands;
        band += smoothstep(edge - softness, edge + softness, lit);
    }
    return band / max(bands, 1.0f);
}

// Cel：主光の拡散を段の明るさで平らに塗り、影の側の環境光に影の色を掛け、主光の側の縁を明るくする。
// 鏡面反射の強さはどの受け方でも掛ける。
void HikariApplyStyleShading(HikariPbrShadingContext context, float3 n, float3 l, float3 radiance,
                             float shadowVisibility, inout HikariDirectPbrLobes directional,
                             inout float3 ambientDiffuse)
{
    directional.specular *= gStyleShading0.w;
    const float bands = gStyleShading0.x;
    if (bands < 0.5f)
        return;
    const float lit = saturate(dot(n, l)) * shadowVisibility;
    const float band = HikariStyleCelBand(lit, bands, gStyleShading0.y, gStyleShading0.z);
    const float3 diffuseColor = context.diffuseEnergyWeight * context.baseColor / HIKARI_PI;
    directional.diffuse = diffuseColor * radiance * band;
    ambientDiffuse *= lerp(gStyleShading1.rgb, 1.0f.xxx, band);
    const float rim = smoothstep(0.55f, 0.72f, 1.0f - saturate(context.ndotView)) * band * gStyleShading1.w;
    directional.diffuse += diffuseColor * radiance * rim * 0.6f;
}

#endif
