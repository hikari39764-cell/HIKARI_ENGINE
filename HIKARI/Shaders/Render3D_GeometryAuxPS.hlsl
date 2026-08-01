#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Features/HIKARI_StylizedFeatureMask.hlsli"
#endif

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

#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
struct PSOutput
{
    float4 packedSurface : SV_Target0;
    uint2 featureMask : SV_Target1;
};
#endif

#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
PSOutput main(PSInput input)
#else
float4 main(PSInput input) : SV_TARGET
#endif
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
    float metallic = saturate(materialData.pbrParams.x);
    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        const float2 metallicRoughnessUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS, input.uv, input.uv1);
        const float4 metallicRoughness =
            HikariSampleMaterialTexture(
                materialData.metallicRoughnessTextureDescriptorIndex,
                gLinearWrap,
                metallicRoughnessUv,
                float4(1.0f, 1.0f, 1.0f, 1.0f));
        roughness = clamp(
            roughness * metallicRoughness.g,
            0.04f,
            1.0f);
        metallic = saturate(metallic * metallicRoughness.b);
    }

    const float3 n =
        HikariResolveSurfaceShadingNormal(
            materialData,
            input.normalWS,
            input.tangentWS,
            input.uv,
            input.uv1,
            gLinearWrap);
    const float4 packedSurface =
        HikariPackSceneSurface(n, roughness, metallic);
#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
    PSOutput output;
    output.packedSurface = packedSurface;
    const float4 faceHairParameters = HikariLoadMaterialProgramParameter(
        materialData,
        10u,
        float4(0.04f, 0.0f, 0.42f, 3.0f));
    output.featureMask = uint2(
        HikariPackStylizedFaceHairParameters(
            HikariBuildStylizedFeatureMask(instance.materialProgramFlags),
            faceHairParameters.z,
            faceHairParameters.w),
        HikariResolveStylizedGroupKey(
            instance.postGroupMask,
            instance.objectIdLow,
            instance.objectIdHigh));
    return output;
#else
    return packedSurface;
#endif
}
