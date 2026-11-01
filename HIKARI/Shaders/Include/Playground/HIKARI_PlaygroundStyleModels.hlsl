// ShaderPlayground の内蔵の試験対象：描画の様式の着色モデル（RenderStyleShading）。params[0..1] は C++ の
// PackRenderStyleShading が詰めた値で、前向きの描画の gStyleShading0/1 と同じ並び。主光と一様な環境光で、
// 前向きの描画と同じ HikariApplyPackedStyleShading を通す。
#include "Include/Playground/HIKARI_Playground.hlsli"
#include "Include/Forward/HIKARI_StyleShadingModels.hlsli"

float4 Play(PlaygroundSample s)
{
    const HikariPbrShadingContext context = PlaygroundPbrContext(s);
    HikariDirectPbrLobes direct = HikariEvaluateDirectPbrLobes(context, s.light, s.lightColor);
    float3 ambientDiffuse = s.ambient * context.diffuseEnergyWeight * context.baseColor;
    HikariApplyPackedStyleShading(s.params[0], s.params[1], context, s.normal, s.light, s.lightColor, 1.0f, direct,
                                  ambientDiffuse);
    const float3 ambientSpecular = s.ambient * context.environmentSpecularResponse;
    return float4(direct.diffuse + direct.specular + ambientDiffuse + ambientSpecular, 1.0f);
}
