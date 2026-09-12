#ifndef HIKARI_MATERIAL_BASE_COLOR_INPUTS_INCLUDED
#define HIKARI_MATERIAL_BASE_COLOR_INPUTS_INCLUDED

#include "../HIKARI_MeshMaterialData.hlsli"

// Alpha も RGB と同じ材質係数と authored UV を使用する。
float4 HikariResolveMaterialBaseColor(
    HikariMeshMaterialData materialData, float2 uv0, float2 uv1, SamplerState materialSampler)
{
    float4 baseColor = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0u)
    {
        const float2 uv = HikariResolveMaterialUv(
            materialData, HIKARI_MATERIAL_UV_BASE_COLOR, uv0, uv1);
        baseColor *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex, materialSampler, uv, 1.0f.xxxx);
    }
    return baseColor;
}

#endif
