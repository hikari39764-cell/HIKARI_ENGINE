#ifndef HIKARI_LIGHTING_TRANSPORT_MATERIAL_INCLUDED
#define HIKARI_LIGHTING_TRANSPORT_MATERIAL_INCLUDED

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#define HIKARI_MESH_MATERIAL_DATA_EXTERNAL_INDEX_CB 1
#define HIKARI_MATERIAL_EXPLICIT_LOD 0.0f
#include "../Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "../Material/HIKARI_MaterialExpressionSurface.hlsli"

struct HikariLightingTransportMaterial
{
    float3 diffuse;
    float3 emissive;
    float3 normal;
};

HikariLightingTransportMaterial HikariEvaluateLightingTransportMaterial(
    uint materialIndex, float2 uv0, float2 uv1, float3 position,
    float3 geometricNormal, float4 tangent, SamplerState materialSampler)
{
    const HikariMeshMaterialData material = HikariGetMeshMaterialData(materialIndex);
    float4 baseColor = HikariResolveMaterialBaseColor(material, uv0, uv1, materialSampler);
    HikariMaterialPbrInputs pbr = HikariResolveMaterialPbrInputs(material, uv0, uv1, materialSampler);
    HikariLightingTransportMaterial result;
    result.emissive = HikariResolveMaterialEmissive(material, uv0, uv1, materialSampler);
    result.normal = HikariResolveSurfaceShadingNormal(material, geometricNormal, tangent, uv0, uv1, materialSampler);
    // 静的な表面入力のみ使用。time / view / screen / derivative は CPU が事前に拒否する。
    const HikariMaterialExpressionContext context = HikariBuildMaterialExpressionContext(
        uv0, uv1, 0.0f.xxxx, position, geometricNormal, tangent,
        position + geometricNormal, 0.0f.xxxx, 0.0f.xx);
    const HikariMaterialExpressionResult expression = HikariEvaluateMaterialExpression(material, context, materialSampler);
    float alphaCutoff = material.pbrParams.w;
    HikariApplyMaterialExpressionCoverage(expression, baseColor, alphaCutoff);
    HikariApplyMaterialExpressionPbr(expression, pbr);
    result.normal = HikariResolveMaterialExpressionNormal(expression, material.normalScale, geometricNormal, tangent, result.normal);
    if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_EMISSIVE) != 0u) result.emissive = expression.emissive;
    result.diffuse = saturate(baseColor.rgb) * (1.0f - pbr.metallic);
    if ((material.materialFlags & HIKARI_MATERIAL_FLAG_UNLIT) != 0u)
    {
        result.diffuse = 0.0f.xxx;
        result.emissive += max(0.0f.xxx, baseColor.rgb);
    }
    return result;
}

#endif
