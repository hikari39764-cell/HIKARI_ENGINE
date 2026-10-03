#define HIKARI_MASS_GPU_SCENE_DISABLED 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"
#include "Include/Surface/HIKARI_TerrainSurfaceParams.hlsli"
#include "Include/Lighting/HIKARI_ShadowMaterialCoverage.hlsli"

SamplerState gLinearWrap : register(s0);
static const uint MATERIAL_ALPHA_MASK = HIKARI_MATERIAL_FLAG_ALPHA_MASK;

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
    nointerpolation uint materialDataIndex : MATERIALINDEX;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD2;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD3;
    float3 worldPosWS : TEXCOORD4;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
};

void main(PSInput input)
{
    if (HikariSurfaceHasTerrainSurface(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance terrainInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        const HikariTerrainSurfaceParams terrainParams =
            HikariLoadTerrainSurfaceParams(
                terrainInstance.surfaceFeatureDataIndex);
        HikariClipTerrainCoverage(terrainParams, input.uv1);
    }

    if ((input.materialFlags & MATERIAL_ALPHA_MASK) == 0)
    {
        return;
    }

    const HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    HikariClipShadowMaterial(materialData, input.uv, input.uv1,
        input.worldPosWS, input.normalWS, input.tangentWS, gLinearWrap);
}
