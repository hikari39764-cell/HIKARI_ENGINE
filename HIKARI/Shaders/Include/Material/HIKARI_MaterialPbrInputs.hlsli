#ifndef HIKARI_MATERIAL_PBR_INPUTS_INCLUDED
#define HIKARI_MATERIAL_PBR_INPUTS_INCLUDED

#include "../HIKARI_MeshMaterialData.hlsli"
#include "HIKARI_PbrMaterialPolicy.hlsli"

struct HikariMaterialPbrInputs
{
    float metallic;
    float roughness;
    float occlusion;
    float3 specularColor;
    float specularFactor;
};

// 材質テクスチャと旧資産の応答を一度だけ解決する。
// Forward と SceneSurface は同じ UV、チャンネル、係数を消費する。
HikariMaterialPbrInputs HikariResolveMaterialPbrInputs(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    SamplerState materialSampler,
    HikariMaterialTextureFootprint footprint = (HikariMaterialTextureFootprint)0)
{
    HikariMaterialPbrInputs inputs;
    inputs.metallic = saturate(materialData.pbrParams.x);
    inputs.roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    inputs.occlusion = 1.0f;
    inputs.specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
    inputs.specularFactor = max(0.0f, materialData.specularParams.w);
    if (materialData.hasMetallicRoughnessTexture != 0u)
    {
        const float4 mr = HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS, materialData.metallicRoughnessTextureDescriptorIndex,
            materialSampler, uv0, uv1, 1.0f.xxxx, footprint);
        inputs.roughness = clamp(inputs.roughness * mr.g, 0.04f, 1.0f);
        inputs.metallic = saturate(inputs.metallic * mr.b);
    }
    if (materialData.hasOcclusionTexture != 0u)
    {
        const float ao = HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_OCCLUSION, materialData.occlusionTextureDescriptorIndex,
            materialSampler, uv0, uv1, 1.0f.xxxx, footprint).r;
        inputs.occlusion = lerp(1.0f, ao, saturate(materialData.pbrParams.z));
    }
    if (materialData.hasSpecularTexture != 0u)
    {
        inputs.specularFactor *= HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_SPECULAR, materialData.specularTextureDescriptorIndex,
            materialSampler, uv0, uv1, 1.0f.xxxx, footprint).a;
    }
    if (materialData.hasSpecularColorTexture != 0u)
    {
        inputs.specularColor *= HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_SPECULAR_COLOR, materialData.specularColorTextureDescriptorIndex,
            materialSampler, uv0, uv1, 1.0f.xxxx, footprint).rgb;
    }
    HikariApplyLegacySpecularResponse(
        (materialData.materialFlags & HIKARI_MATERIAL_FLAG_LEGACY_SPECULAR) != 0u,
        (materialData.materialFlags & HIKARI_MATERIAL_FLAG_ALPHA_MASK) != 0u,
        inputs.roughness, inputs.specularColor, inputs.specularFactor);
    return inputs;
}

float3 HikariResolveMaterialEmissive(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    SamplerState materialSampler,
    HikariMaterialTextureFootprint footprint = (HikariMaterialTextureFootprint)0)
{
    float3 emissive = materialData.emissiveFactor.rgb;
    if (materialData.hasEmissiveTexture != 0u)
    {
        emissive *= HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_EMISSIVE, materialData.emissiveTextureDescriptorIndex,
            materialSampler, uv0, uv1, float4(0.0f, 0.0f, 0.0f, 1.0f), footprint).rgb;
    }
    return emissive * materialData.emissiveFactor.a;
}

#endif
