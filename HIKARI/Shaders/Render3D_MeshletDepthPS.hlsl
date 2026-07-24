static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint HIKARI_INVALID_MESHLET_DEPTH_MATERIAL_INDEX = 0xffffffffu;

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"

SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    float3 worldPosWS : TEXCOORD3;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
};

void main(PSInput input)
{
    if (input.materialDataIndex == HIKARI_INVALID_MESHLET_DEPTH_MATERIAL_INDEX)
    {
        return;
    }

    if (HikariSurfaceHasMaterialFx(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance instance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariApplyStaticMaterialFxCoverage(
            input.worldPosWS,
            instance.fxUser[0],
            instance.fxUser[1]);
    }

    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_BASE_COLOR,
                input.uv,
                input.uv1);
        const float alpha =
            HikariSampleMaterialTexture(
                materialData.baseColorTextureDescriptorIndex,
                gLinearWrap,
                baseColorUv,
                float4(1.0f, 1.0f, 1.0f, 1.0f)).a;
        if (alpha < materialData.pbrParams.w)
        {
            discard;
        }
    }
}
