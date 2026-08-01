#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1u;
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    const HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_FEATURE_VISIBILITY_OVERLAY) == 0u)
    {
        discard;
    }

    float4 baseColor = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0u)
    {
        const float2 baseColorUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_BASE_COLOR,
            input.uv,
            input.uv1);
        baseColor *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            1.0f.xxxx);
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u &&
        baseColor.a < materialData.pbrParams.w)
    {
        discard;
    }
    const float3 overlay = lerp(baseColor.rgb, 1.0f.xxx, 0.12f);
    return float4(overlay, saturate(baseColor.a * 0.72f));
}
