#ifndef HIKARI_STYLE_SHADING_MODELS_HLSLI
#define HIKARI_STYLE_SHADING_MODELS_HLSLI

// 描画の様式が選ぶ着色モデルの本体。引数だけで計算し、エンジンの定数バッファを読まない（ShaderPlayground からも
// 同じ関数を呼ぶ）。前向きの描画は HIKARI_StyleShading.hlsli が LightCB の値を渡す。
// 登録表は RenderContracts/RenderStyle/HIKARI_RenderStyleShading.h（Physical は鏡面反射の強さだけで、ここに本体はない）。
#include "Include/HIKARI_PbrCommon.hlsli"

// Cel の値。並びは詰めた 2 つの float4（HikariApplyPackedStyleShading が詰め替える）と同じ。
struct HikariCelStyleParams
{
    float bands;      // 明るい側の段数
    float threshold;  // これより暗い当たりが影
    float softness;   // 段の境のぼかし（当たりの量の幅）
    float3 shadowTint; // 影の側の環境光に掛ける色
    float rim;        // 主光の側の縁の光
};

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
void HikariShadeCelStyle(HikariPbrShadingContext context, float3 n, float3 l, float3 radiance, float shadowVisibility,
                         HikariCelStyleParams params, inout HikariDirectPbrLobes directional,
                         inout float3 ambientDiffuse)
{
    const float lit = saturate(dot(n, l)) * shadowVisibility;
    const float band = HikariStyleCelBand(lit, params.bands, params.threshold, params.softness);
    const float3 diffuseColor = context.diffuseEnergyWeight * context.baseColor / HIKARI_PI;
    directional.diffuse = diffuseColor * radiance * band;
    ambientDiffuse *= lerp(params.shadowTint, 1.0f.xxx, band);
    const float rim = smoothstep(0.55f, 0.72f, 1.0f - saturate(context.ndotView)) * band * params.rim;
    directional.diffuse += diffuseColor * radiance * rim * 0.6f;
}

// 詰めた値（C++ の PackRenderStyleShading と対）でモデルを選んで当てる。鏡面反射の強さはどのモデルでも掛ける。
// 段数 0 は Physical（それ以外は変えない）。
void HikariApplyPackedStyleShading(float4 shading0, float4 shading1, HikariPbrShadingContext context, float3 n,
                                   float3 l, float3 radiance, float shadowVisibility,
                                   inout HikariDirectPbrLobes directional, inout float3 ambientDiffuse)
{
    directional.specular *= shading0.w;
    if (shading0.x < 0.5f)
        return;
    HikariCelStyleParams params;
    params.bands = shading0.x;
    params.threshold = shading0.y;
    params.softness = shading0.z;
    params.shadowTint = shading1.rgb;
    params.rim = shading1.w;
    HikariShadeCelStyle(context, n, l, radiance, shadowVisibility, params, directional, ambientDiffuse);
}

#endif
