#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
};

static const uint MATERIAL_ALPHA_MASK = 1u << 1u;
SamplerState gLinearWrap : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    const HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_FEATURE_GEOMETRY_OUTLINE) == 0u)
    {
        discard;
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u)
    {
        float alpha = materialData.baseColor.a;
        if (materialData.hasBaseColorTexture != 0u)
        {
            const float2 baseColorUv = HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_BASE_COLOR,
                input.uv,
                input.uv1);
            alpha *= HikariSampleMaterialTexture(
                materialData.baseColorTextureDescriptorIndex,
                gLinearWrap,
                baseColorUv,
                1.0f.xxxx).a;
        }
        if (alpha < materialData.pbrParams.w)
        {
            discard;
        }
    }
    const float4 outline = HikariLoadMaterialProgramParameter(
        materialData,
        19u,
        float4(0.025f, 0.030f, 0.045f, 1.25f));
    return float4(max(outline.rgb, 0.0f.xxx), 1.0f);
}
