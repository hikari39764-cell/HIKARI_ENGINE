#ifndef HIKARI_MATERIAL_BASE_COLOR_INPUTS_INCLUDED
#define HIKARI_MATERIAL_BASE_COLOR_INPUTS_INCLUDED

#include "../HIKARI_MeshMaterialData.hlsli"
#include "HIKARI_MaterialProgramData.hlsli"

// Alpha も RGB と同じ材質係数と authored UV を使用する。
float4 HikariResolveMaterialBaseColor(
    HikariMeshMaterialData materialData, float2 uv0, float2 uv1, SamplerState materialSampler,
    bool unassociateSource = false,
    HikariMaterialTextureFootprint footprint = (HikariMaterialTextureFootprint)0)
{
    float4 baseColor = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0u)
    {
        float4 sampleColor = HikariSampleMaterialTextureSlot(
            materialData, HIKARI_MATERIAL_UV_BASE_COLOR, materialData.baseColorTextureDescriptorIndex,
            materialSampler, uv0, uv1, 1.0f.xxxx, footprint);
        if (unassociateSource) sampleColor = HikariResolveMaterialSourceColor(materialData, sampleColor);
        baseColor *= sampleColor;
    }
    return baseColor;
}

#endif
