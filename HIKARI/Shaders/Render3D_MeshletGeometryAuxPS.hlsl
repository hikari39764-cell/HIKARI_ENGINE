#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1;

SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    float3 worldPosWS : TEXCOORD3;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
};

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_BASE_COLOR,
                input.uv,
                input.uv1);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u &&
        albedo.a < materialData.pbrParams.w)
    {
        discard;
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

    float roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        const float2 metallicRoughnessUv =
            HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS,
                input.uv,
                input.uv1);
        roughness = clamp(
            roughness *
                HikariSampleMaterialTexture(
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
