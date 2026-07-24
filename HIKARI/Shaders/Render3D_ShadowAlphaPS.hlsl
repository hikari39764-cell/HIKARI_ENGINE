static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint HIKARI_INVALID_SHADOW_MATERIAL_INDEX = 0xffffffffu;

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"

Texture2D gBaseColorTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
    nointerpolation uint materialDataIndex : MATERIALINDEX;
};

void main(PSInput input)
{
    if ((input.materialFlags & MATERIAL_ALPHA_MASK) == 0)
    {
        return;
    }

    float alpha = 1.0f;
    if (input.materialDataIndex != HIKARI_INVALID_SHADOW_MATERIAL_INDEX)
    {
        HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
        const float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        alpha = HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).a;
    }
    else
    {
        alpha = gBaseColorTex.Sample(gLinearWrap, input.uv).a;
    }

    if (alpha < input.alphaCutoff)
    {
        discard;
    }
}
