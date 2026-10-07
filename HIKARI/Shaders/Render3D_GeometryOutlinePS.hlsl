#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/HIKARI_SurfaceGpuScene.hlsli"

typedef HikariForwardPixelInput PSInput;

static const uint MATERIAL_ALPHA_MASK = 1u << 1u;
SamplerState gLinearWrap : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    // 選択/geometry outline は基底の輪郭を使い、筆触の全層を重ねない。
    if (input.shellLayer.y < 0.5f) discard;
    const HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    uint surfaceMaterialProgramFlags = 0u;
    if (gUseSurfaceGpuScene != 0u)
    {
        surfaceMaterialProgramFlags =
            HikariGetSurfaceGpuSceneInstanceAt(
                input.surfaceGpuSceneIndex).materialProgramFlags;
    }
    const bool authoredOutline =
        ((surfaceMaterialProgramFlags |
          materialData.instanceFlags) &
         HIKARI_PROGRAM_FLAG_FEATURE_GEOMETRY_OUTLINE) != 0u;
    const bool editorSelection =
        (surfaceMaterialProgramFlags &
         HIKARI_PROGRAM_FLAG_EDITOR_SELECTION_OUTLINE) != 0u;
    if (!authoredOutline && !editorSelection)
    {
        discard;
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0u)
    {
        const float alpha = HikariResolveMaterialBaseColor(
            materialData, input.uv, input.uv1, gLinearWrap).a;
        if (alpha < materialData.pbrParams.w)
        {
            discard;
        }
    }
    const float4 outline = editorSelection
        ? float4(0.08f, 0.82f, 1.0f, 2.4f)
        : HikariLoadMaterialProgramParameter(
            materialData,
            19u,
            float4(0.025f, 0.030f, 0.045f, 2.0f));
    return float4(max(outline.rgb, 0.0f.xxx), 1.0f);
}
