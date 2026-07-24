#ifndef HIKARI_MESH_MATERIAL_DATA_INCLUDED
#define HIKARI_MESH_MATERIAL_DATA_INCLUDED

#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"

// Keep this layout in sync with C++ MaterialGpuData.
struct HikariMeshMaterialData
{
    float4 baseColor;
    float4 emissiveFactor;
    float4 pbrParams;
    float4 specularParams;
    uint materialFlags;
    uint hasBaseColorTexture;
    uint hasNormalTexture;
    uint hasEmissiveTexture;
    uint hasMetallicRoughnessTexture;
    uint hasOcclusionTexture;
    uint hasSpecularTexture;
    uint hasSpecularColorTexture;
    float normalScale;
    float3 materialPadding0;
    int baseColorTextureHandle;
    int normalTextureHandle;
    int emissiveTextureHandle;
    int metallicRoughnessTextureHandle;
    int occlusionTextureHandle;
    int specularTextureHandle;
    int specularColorTextureHandle;
    uint baseColorTextureDescriptorIndex;
    uint normalTextureDescriptorIndex;
    uint emissiveTextureDescriptorIndex;
    uint metallicRoughnessTextureDescriptorIndex;
    uint occlusionTextureDescriptorIndex;
    uint specularTextureDescriptorIndex;
    uint specularColorTextureDescriptorIndex;
    uint2 materialPadding1;
    float4 baseColorUvTransform;
    float4 normalUvTransform;
    float4 emissiveUvTransform;
    float4 metallicRoughnessUvTransform;
    float4 occlusionUvTransform;
    float4 specularUvTransform;
    float4 specularColorUvTransform;
    float4 uvRotation0;
    float4 uvRotation1;
    uint4 uvSet0;
    uint4 uvSet1;
};

StructuredBuffer<HikariMeshMaterialData> gMaterialDataBuffer : register(t16);

cbuffer MaterialIndexCB : register(b7)
{
    uint gMaterialDataIndex;
    uint3 gMaterialDataPadding;
};

// instance ごとの material index から MaterialData を読む。
static const uint HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX = 0xffffffffu;

HikariMeshMaterialData HikariBuildDefaultMeshMaterialData()
{
    HikariMeshMaterialData data = (HikariMeshMaterialData)0;
    data.baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    data.pbrParams = float4(0.0f, 1.0f, 1.0f, 0.5f);
    data.specularParams = float4(1.0f, 1.0f, 1.0f, 1.0f);
    data.normalScale = 1.0f;
    data.baseColorTextureHandle = -1;
    data.normalTextureHandle = -1;
    data.emissiveTextureHandle = -1;
    data.metallicRoughnessTextureHandle = -1;
    data.occlusionTextureHandle = -1;
    data.specularTextureHandle = -1;
    data.specularColorTextureHandle = -1;
    data.baseColorTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.normalTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.emissiveTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.metallicRoughnessTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.occlusionTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.specularTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.specularColorTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.baseColorUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.normalUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.emissiveUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.metallicRoughnessUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.occlusionUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.specularUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    data.specularColorUvTransform = float4(1.0f, 1.0f, 0.0f, 0.0f);
    return data;
}

HikariMeshMaterialData HikariGetMeshMaterialData(uint materialDataIndex)
{
    if (materialDataIndex == HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX)
    {
        return HikariBuildDefaultMeshMaterialData();
    }
    return gMaterialDataBuffer[materialDataIndex];
}

#define gMaterialData gMaterialDataBuffer[gMaterialDataIndex]
#define gBaseColor gMaterialData.baseColor
#define gMaterialFlags gMaterialData.materialFlags
#define gHasBaseColorTexture gMaterialData.hasBaseColorTexture
#define gHasNormalTexture gMaterialData.hasNormalTexture
#define gHasEmissiveTexture gMaterialData.hasEmissiveTexture
#define gHasMetallicRoughnessTexture gMaterialData.hasMetallicRoughnessTexture
#define gHasOcclusionTexture gMaterialData.hasOcclusionTexture
#define gHasSpecularTexture gMaterialData.hasSpecularTexture
#define gHasSpecularColorTexture gMaterialData.hasSpecularColorTexture
#define gNormalScale gMaterialData.normalScale
#define gAlphaCutoff gMaterialData.pbrParams.w
#define gEmissiveFactor gMaterialData.emissiveFactor
#define gMetallicFactor gMaterialData.pbrParams.x
#define gRoughnessFactor gMaterialData.pbrParams.y
#define gOcclusionStrength gMaterialData.pbrParams.z
#define gSpecularFactor gMaterialData.specularParams.w
#define gSpecularColorFactor gMaterialData.specularParams.rgb
#define gBaseColorTextureDescriptorIndex gMaterialData.baseColorTextureDescriptorIndex
#define gNormalTextureDescriptorIndex gMaterialData.normalTextureDescriptorIndex
#define gEmissiveTextureDescriptorIndex gMaterialData.emissiveTextureDescriptorIndex
#define gMetallicRoughnessTextureDescriptorIndex gMaterialData.metallicRoughnessTextureDescriptorIndex
#define gOcclusionTextureDescriptorIndex gMaterialData.occlusionTextureDescriptorIndex
#define gSpecularTextureDescriptorIndex gMaterialData.specularTextureDescriptorIndex
#define gSpecularColorTextureDescriptorIndex gMaterialData.specularColorTextureDescriptorIndex

static const uint HIKARI_MATERIAL_UV_BASE_COLOR = 0u;
static const uint HIKARI_MATERIAL_UV_NORMAL = 1u;
static const uint HIKARI_MATERIAL_UV_EMISSIVE = 2u;
static const uint HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS = 3u;
static const uint HIKARI_MATERIAL_UV_OCCLUSION = 4u;
static const uint HIKARI_MATERIAL_UV_SPECULAR = 5u;
static const uint HIKARI_MATERIAL_UV_SPECULAR_COLOR = 6u;

float4 HikariGetMaterialUvTransform(HikariMeshMaterialData materialData, uint slot)
{
    if (slot == HIKARI_MATERIAL_UV_BASE_COLOR) return materialData.baseColorUvTransform;
    if (slot == HIKARI_MATERIAL_UV_NORMAL) return materialData.normalUvTransform;
    if (slot == HIKARI_MATERIAL_UV_EMISSIVE) return materialData.emissiveUvTransform;
    if (slot == HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS) return materialData.metallicRoughnessUvTransform;
    if (slot == HIKARI_MATERIAL_UV_OCCLUSION) return materialData.occlusionUvTransform;
    if (slot == HIKARI_MATERIAL_UV_SPECULAR) return materialData.specularUvTransform;
    return materialData.specularColorUvTransform;
}

float HikariGetMaterialUvRotation(HikariMeshMaterialData materialData, uint slot)
{
    if (slot == HIKARI_MATERIAL_UV_BASE_COLOR) return materialData.uvRotation0.x;
    if (slot == HIKARI_MATERIAL_UV_NORMAL) return materialData.uvRotation0.y;
    if (slot == HIKARI_MATERIAL_UV_EMISSIVE) return materialData.uvRotation0.z;
    if (slot == HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS) return materialData.uvRotation0.w;
    if (slot == HIKARI_MATERIAL_UV_OCCLUSION) return materialData.uvRotation1.x;
    if (slot == HIKARI_MATERIAL_UV_SPECULAR) return materialData.uvRotation1.y;
    return materialData.uvRotation1.z;
}

uint HikariGetMaterialUvSet(HikariMeshMaterialData materialData, uint slot)
{
    if (slot == HIKARI_MATERIAL_UV_BASE_COLOR) return materialData.uvSet0.x;
    if (slot == HIKARI_MATERIAL_UV_NORMAL) return materialData.uvSet0.y;
    if (slot == HIKARI_MATERIAL_UV_EMISSIVE) return materialData.uvSet0.z;
    if (slot == HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS) return materialData.uvSet0.w;
    if (slot == HIKARI_MATERIAL_UV_OCCLUSION) return materialData.uvSet1.x;
    if (slot == HIKARI_MATERIAL_UV_SPECULAR) return materialData.uvSet1.y;
    return materialData.uvSet1.z;
}

float2 HikariApplyMaterialUvTransform(float2 uv, float4 transform, float rotation)
{
    float2 result = uv * transform.xy;
    if (abs(rotation) > 1.0e-6f)
    {
        float s = sin(rotation);
        float c = cos(rotation);
        result = float2(
            result.x * c - result.y * s,
            result.x * s + result.y * c);
    }
    return result + transform.zw;
}

float2 HikariResolveMaterialUv(HikariMeshMaterialData materialData, uint slot, float2 uv0, float2 uv1)
{
    float2 selectedUv = HikariGetMaterialUvSet(materialData, slot) == 1u ? uv1 : uv0;
    return HikariApplyMaterialUvTransform(
        selectedUv,
        HikariGetMaterialUvTransform(materialData, slot),
        HikariGetMaterialUvRotation(materialData, slot));
}

#if defined(HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING)
static const uint HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX = 0xffffffffu;
static const uint HIKARI_MATERIAL_TEXTURE_POOL_COUNT = HIKARI_SHADER_USER_SRV_COUNT;

// 材質テクスチャは descriptor index で SRV プールから参照する。
Texture2D gMaterialTexturePool[HIKARI_MATERIAL_TEXTURE_POOL_COUNT] : register(t20);

bool HikariHasMaterialTexture(uint descriptorIndex)
{
    return descriptorIndex != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX &&
        descriptorIndex < HIKARI_MATERIAL_TEXTURE_POOL_COUNT;
}

float4 HikariSampleMaterialTexture(
    uint descriptorIndex,
    SamplerState samplerState,
    float2 uv,
    float4 fallbackValue)
{
    if (!HikariHasMaterialTexture(descriptorIndex))
    {
        return fallbackValue;
    }

    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Sample(samplerState, uv);
}
#endif

#endif
