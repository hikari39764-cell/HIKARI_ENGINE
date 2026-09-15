#ifndef HIKARI_MESH_MATERIAL_DATA_INCLUDED
#define HIKARI_MESH_MATERIAL_DATA_INCLUDED

#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"

// C++ MATERIAL_FEATURES の wire bits。描画パスごとに再定義しない。
static const uint HIKARI_MATERIAL_FLAG_UNLIT = 1u << 0u;
static const uint HIKARI_MATERIAL_FLAG_ALPHA_MASK = 1u << 1u;
static const uint HIKARI_MATERIAL_FLAG_EMISSIVE = 1u << 2u;
static const uint HIKARI_MATERIAL_FLAG_ALPHA_BLEND = 1u << 3u;
static const uint HIKARI_MATERIAL_FLAG_LEGACY_SPECULAR = 1u << 4u;

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
    uint materialProgramIndex;
    uint parameterBlockOffset;
    uint resourceTableOffset;
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
    uint parameterBlockCount;
    uint instanceFlags;
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

#if !defined(HIKARI_MESH_MATERIAL_DATA_EXTERNAL_INDEX_CB)
cbuffer MaterialIndexCB : register(b7)
{
    uint gMaterialDataIndex;
    uint3 gMaterialDataPadding;
};
#endif

// instance ごとの material index から MaterialData を読む。
static const uint HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX = 0xffffffffu;

HikariMeshMaterialData HikariBuildDefaultMeshMaterialData()
{
    HikariMeshMaterialData data = (HikariMeshMaterialData)0;
    data.baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    data.pbrParams = float4(0.0f, 1.0f, 1.0f, 0.5f);
    data.specularParams = float4(1.0f, 1.0f, 1.0f, 1.0f);
    data.normalScale = 1.0f;
    data.materialProgramIndex = 0u;
    data.parameterBlockOffset = 0u;
    data.resourceTableOffset = 0u;
    data.parameterBlockCount = 0u;
    data.instanceFlags = 0u;
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

static const uint HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX = 0xffffffffu;
static const uint HIKARI_MATERIAL_TEXTURE_POOL_COUNT = HIKARI_SHADER_USER_SRV_COUNT;

// Raster は既定の暗黙微分、ray consumer は交差面上の明示的 footprint を渡す。
// xy=UV0, zw=UV1。共有/可変の shader global に格納しない。
struct HikariMaterialTextureFootprint
{
    float4 uvDx;
    float4 uvDy;
    bool explicitGradients;
};

float2 HikariTransformMaterialUvGradient(float2 gradient, float4 transform, float rotation)
{
    return HikariApplyMaterialUvTransform(gradient, float4(transform.xy, 0.0f.xx), rotation);
}

#if defined(HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING)

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

#if defined(HIKARI_MATERIAL_EXPLICIT_LOD)
    // Compute / ray query には raster derivative がない。オフライン表面は原解像度を使用する。
    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleLevel(samplerState, uv, HIKARI_MATERIAL_EXPLICIT_LOD);
#else
    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Sample(samplerState, uv);
#endif
}

float4 HikariSampleMaterialTextureGrad(
    uint descriptorIndex,
    SamplerState samplerState,
    float2 uv,
    float2 uvDx,
    float2 uvDy,
    float4 fallbackValue)
{
    if (!HikariHasMaterialTexture(descriptorIndex))
    {
        return fallbackValue;
    }

    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].SampleGrad(
        samplerState,
        uv,
        uvDx,
        uvDy);
}

float4 HikariSampleMaterialTextureSlot(
    HikariMeshMaterialData materialData, uint slot, uint descriptorIndex,
    SamplerState samplerState, float2 uv0, float2 uv1, float4 fallbackValue,
    HikariMaterialTextureFootprint footprint)
{
    const float2 uv = HikariResolveMaterialUv(materialData, slot, uv0, uv1);
    if (!footprint.explicitGradients)
        return HikariSampleMaterialTexture(descriptorIndex, samplerState, uv, fallbackValue);
    const bool secondary = HikariGetMaterialUvSet(materialData, slot) == 1u;
    const float4 transform = HikariGetMaterialUvTransform(materialData, slot);
    const float rotation = HikariGetMaterialUvRotation(materialData, slot);
    return HikariSampleMaterialTextureGrad(descriptorIndex, samplerState, uv,
        HikariTransformMaterialUvGradient(secondary ? footprint.uvDx.zw : footprint.uvDx.xy, transform, rotation),
        HikariTransformMaterialUvGradient(secondary ? footprint.uvDy.zw : footprint.uvDy.xy, transform, rotation),
        fallbackValue);
}
#endif

#endif
