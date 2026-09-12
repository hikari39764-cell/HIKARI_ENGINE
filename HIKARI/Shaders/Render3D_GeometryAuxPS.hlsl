#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/Material/HIKARI_MaterialPbrInputs.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceNormal.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
#include "Include/Features/HIKARI_StylizedFeatureMask.hlsli"
#endif

static const uint MATERIAL_ALPHA_MASK = HIKARI_MATERIAL_FLAG_ALPHA_MASK;

SamplerState gLinearWrap : register(s0);
SamplerState gLinearClamp : register(s2);

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
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
    bool isFrontFace : SV_IsFrontFace;
};

#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
struct PSOutput
{
    float4 packedSurface : SV_Target0;
    uint2 featureMask : SV_Target1;
    float4 outlineStyle : SV_Target2;
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
    if (HikariSurfaceHasSandSurface(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance sandInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariClipSandCoverage(
            HikariLoadSandSurfaceParamsFromDataIndex(
                sandInstance.surfaceFeatureDataIndex),
            input.uv1);
    }

    const HikariMaterialPbrInputs pbrInputs = HikariResolveMaterialPbrInputs(
        materialData, input.uv, input.uv1, gLinearWrap);
    float roughness = pbrInputs.roughness;
    const float metallic = pbrInputs.metallic;

    const float3 geometricNormal =
        (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u && !input.isFrontFace
            ? -input.normalWS : input.normalWS;
    float3 n = normalize(geometricNormal);
    if (HikariSurfaceHasSandSurface(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance sandInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        n = HikariResolveSandSurfaceAuxNormal(
            materialData,
            HikariLoadSandSurfaceParamsFromDataIndex(
                sandInstance.surfaceFeatureDataIndex),
            input.worldPosWS,
            input.uv1,
            input.normalWS,
            input.tangentWS,
            gLinearClamp);
    }
    else
    {
        n = HikariResolveSurfaceShadingNormal(
            materialData,
            geometricNormal,
            input.tangentWS,
            input.uv,
            input.uv1,
            gLinearWrap);
    }
    if (!HikariSurfaceHasSandSurface(input.surfaceFeatureFlags))
    {
        roughness = HikariFilterPerceptualRoughness(roughness, ddx(n), ddy(n));
    }
    const float4 packedSurface = HikariPackSceneSurface(n, roughness, metallic);
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
    output.outlineStyle = HikariEncodeStylizedOutlineStyle(
        HikariLoadMaterialProgramParameter(
            materialData,
            19u,
            float4(0.025f, 0.030f, 0.045f, 2.0f)));
    return output;
#else
    return packedSurface;
#endif
}
