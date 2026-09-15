#ifndef HIKARI_MATERIAL_EXPRESSION_SURFACE_INCLUDED
#define HIKARI_MATERIAL_EXPRESSION_SURFACE_INCLUDED

#include "HIKARI_MaterialPbrInputs.hlsli"
#include "HIKARI_MaterialProgramData.hlsli"
#include "HIKARI_MaterialExpression.hlsli"
#include "../Surface/HIKARI_SurfaceShadingNormal.hlsli"

// ラスタライズされた同じ表面から全パスの式入力を組み立てる。
HikariMaterialExpressionContext HikariBuildMaterialExpressionContext(
    float2 uv0, float2 uv1, float4 time,
    float3 worldPosition, float3 geometricNormal, float4 worldTangent,
    float3 cameraPosition, float4 pixelPosition, float2 inverseScreenSize,
    HikariMaterialTextureFootprint textureFootprint = (HikariMaterialTextureFootprint)0)
{
    HikariMaterialExpressionContext context;
    context.uv0 = uv0;
    context.uv1 = uv1;
    context.time = time;
    context.worldPosition = worldPosition;
    context.worldNormal = geometricNormal;
    context.worldTangent = worldTangent;
    const float3 toCamera = cameraPosition - worldPosition;
    context.viewDirection = toCamera / max(length(toCamera), 0.00001f);
    context.cameraPosition = cameraPosition;
    context.screenPosition = float4(
        pixelPosition.xy * inverseScreenSize, pixelPosition.zw);
    context.textureFootprint = textureFootprint;
    return context;
}

void HikariApplyMaterialExpressionCoverage(
    HikariMaterialExpressionResult expression,
    inout float4 baseColor, inout float alphaClipThreshold)
{
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_BASE_COLOR) != 0u)
        baseColor = expression.baseColor;
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_OPACITY) != 0u)
        baseColor.a = saturate(expression.opacity);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_ALPHA_CLIP) != 0u)
        alphaClipThreshold = saturate(expression.alphaClipThreshold);
}

void HikariApplyMaterialExpressionPbr(
    HikariMaterialExpressionResult expression, inout HikariMaterialPbrInputs inputs)
{
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_METALLIC) != 0u)
        inputs.metallic = saturate(expression.metallic);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_ROUGHNESS) != 0u)
        inputs.roughness = clamp(expression.roughness, 0.04f, 1.0f);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_OCCLUSION) != 0u)
        inputs.occlusion = saturate(expression.occlusion);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_SPECULAR) != 0u)
        inputs.specularFactor = max(0.0f, expression.specular);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_SPECULAR_COLOR) != 0u)
        inputs.specularColor = max(0.0f.xxx, expression.specularColor);
}

float3 HikariResolveMaterialExpressionNormal(
    HikariMaterialExpressionResult expression, float normalScale,
    float3 geometricNormal, float4 worldTangent, float3 fallbackNormal)
{
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_NORMAL) == 0u)
        return fallbackNormal;
    const float lengthSquared = dot(expression.normalTS, expression.normalTS);
    const float3 normalTS = lengthSquared > 1.0e-10f && isfinite(lengthSquared)
        ? expression.normalTS * rsqrt(lengthSquared) : float3(0.0f, 0.0f, 1.0f);
    // 通常の normal map と同じ XY 強度、正の半球を使用する。
    const float2 normalXY = normalTS.xy * max(normalScale, 0.0f);
    const float3 scaledNormal = float3(normalXY, sqrt(saturate(1.0f - dot(normalXY, normalXY))));
    return HikariTransformTangentNormalToWorld(scaledNormal, geometricNormal, worldTangent);
}

#endif
