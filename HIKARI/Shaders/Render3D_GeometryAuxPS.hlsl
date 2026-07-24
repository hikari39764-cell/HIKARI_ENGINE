#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1;

SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    // Keep TEXCOORD slots aligned between VS and MeshletMS layouts for PSO linkage.
    // forwardMeta bit layout: HIKARI_ForwardVertexMeta.hlsli.
    nointerpolation uint forwardMeta : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugSurfaceId : TEXCOORD6;
};

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < materialData.pbrParams.w)
    {
        discard;
    }

    float roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        const float2 metallicRoughnessUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS, input.uv, input.uv1);
        roughness = clamp(
            roughness * HikariSampleMaterialTexture(
                materialData.metallicRoughnessTextureDescriptorIndex,
                gLinearWrap,
                metallicRoughnessUv,
                float4(1.0f, 1.0f, 1.0f, 1.0f)).g,
            0.04f,
            1.0f);
    }

    float3 n = normalize(input.normalWS);
    return float4(n * 0.5f + 0.5f, roughness);
}
