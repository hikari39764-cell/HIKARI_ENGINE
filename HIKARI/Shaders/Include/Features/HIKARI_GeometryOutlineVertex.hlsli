#ifndef HIKARI_GEOMETRY_OUTLINE_VERTEX_INCLUDED
#define HIKARI_GEOMETRY_OUTLINE_VERTEX_INCLUDED

#include "Include/Material/HIKARI_MaterialProgramData.hlsli"

float4 HikariExpandGeometryOutlineVertex(
    float4 worldPosition,
    float3 normalWS,
    uint materialDataIndex,
    uint surfaceMaterialProgramFlags)
{
    const float4 clipPosition = mul(gViewProj, worldPosition);
    const HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(materialDataIndex);
    const bool authoredOutline =
        ((surfaceMaterialProgramFlags |
          materialData.instanceFlags) &
         HIKARI_PROGRAM_FLAG_FEATURE_GEOMETRY_OUTLINE) != 0u;
    const bool editorSelection =
        (surfaceMaterialProgramFlags &
         HIKARI_PROGRAM_FLAG_EDITOR_SELECTION_OUTLINE) != 0u;
    if (!authoredOutline && !editorSelection)
    {
        return clipPosition;
    }

    const float4 outline = editorSelection
        ? float4(0.08f, 0.82f, 1.0f, 2.4f)
        : HikariLoadMaterialProgramParameter(
            materialData,
            19u,
            float4(0.025f, 0.030f, 0.045f, 2.0f));
    const float4 projectedNormal = mul(
        gViewProj,
        float4(worldPosition.xyz + normalize(normalWS), 1.0f));
    const float2 centerNdc = clipPosition.xy /
        max(abs(clipPosition.w), 0.0001f);
    const float2 normalNdc = projectedNormal.xy /
        max(abs(projectedNormal.w), 0.0001f);
    float2 direction = normalNdc - centerNdc;
    const float directionLength = length(direction);
    if (directionLength < 1.0e-5f)
    {
        direction = float2(0.0f, -1.0f);
    }
    else
    {
        direction /= directionLength;
    }
    const float2 pixelToNdc = 2.0f * gScreenParams.zw;
    float4 expanded = clipPosition;
    expanded.xy += direction * pixelToNdc *
        clamp(outline.w, 0.25f, 8.0f) * clipPosition.w;
    return expanded;
}

#endif
