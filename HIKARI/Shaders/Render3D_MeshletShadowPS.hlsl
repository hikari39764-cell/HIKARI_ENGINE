#define HIKARI_MASS_GPU_SCENE_DISABLED 1
static const uint HIKARI_INVALID_MESHLET_SHADOW_MATERIAL_INDEX = 0xffffffffu;

#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"
#include "Include/Lighting/HIKARI_ShadowMaterialCoverage.hlsli"

SamplerState gLinearWrap : register(s0);
static const uint MATERIAL_ALPHA_MASK = HIKARI_MATERIAL_FLAG_ALPHA_MASK;

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    float3 worldPosWS : TEXCOORD3;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
};

float main(PSInput input) : SV_Depth
{
    if (HikariSurfaceHasSandSurface(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance sandInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        const HikariSandSurfaceParams sandParams =
            HikariLoadSandSurfaceParamsFromDataIndex(
                sandInstance.surfaceFeatureDataIndex);
        HikariClipSandCoverage(sandParams, input.uv1);
    }

    if (input.materialDataIndex == HIKARI_INVALID_MESHLET_SHADOW_MATERIAL_INDEX)
    {
        return input.position.z;
    }

    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    HikariClipShadowMaterial(materialData, input.uv, input.uv1,
        input.worldPosWS, input.normalWS, input.tangentWS, gLinearWrap);
    return input.position.z;
}
