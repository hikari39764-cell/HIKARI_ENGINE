#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1u;
SamplerState gLinearWrap : register(s0);

typedef HikariForwardPixelInput PSInput;

float4 main(PSInput input) : SV_TARGET
{
    const HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_FEATURE_VISIBILITY_OVERLAY) == 0u)
    {
        discard;
    }

    float4 baseColor = HikariResolveMaterialBaseColor(
        materialData, input.uv, input.uv1, gLinearWrap);
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u &&
        baseColor.a < materialData.pbrParams.w)
    {
        discard;
    }
    const float3 overlay = lerp(baseColor.rgb, 1.0f.xxx, 0.12f);
    return float4(overlay, saturate(baseColor.a * 0.72f));
}
