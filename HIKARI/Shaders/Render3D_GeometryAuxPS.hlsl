#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"
#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#include "Include/Surface/HIKARI_MaterialShell.hlsli"
#include "Include/Surface/HIKARI_PainterlyShell.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/Material/HIKARI_MaterialExpressionSurface.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"
#if HIKARI_SURFACE_PARTITION != HIKARI_SURFACE_PARTITION_GENERAL
#include "Include/Surface/HIKARI_TerrainSurfaceParams.hlsli"
#include "Include/Surface/HIKARI_TerrainSurfaceNormal.hlsli"
#endif
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
#include "Include/Features/HIKARI_StylizedFeatureMask.hlsli"
#endif

static const uint MATERIAL_ALPHA_MASK = HIKARI_MATERIAL_FLAG_ALPHA_MASK;

SamplerState gLinearWrap : register(s0);
SamplerState gLinearClamp : register(s2);
#include "Include/Lighting/HIKARI_LocalDecals.hlsli"

#if defined(HIKARI_GEOMETRY_AUX_MESHLET_INPUT)
// Mesh Shader の圧縮した補助パス入力を保ち、材質評価は従来パスと共有する。
struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    float3 worldPosWS : TEXCOORD3;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD4;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD5;
    nointerpolation float4 shellLayer : TEXCOORD6;
    bool isFrontFace : SV_IsFrontFace;
};
#else
typedef HikariForwardPixelInput PSInput;
#endif

// 捨てない面だけを描く PSO（AS が alpha test・材質の特効・材質 program の面を除く）。画素を捨てる命令を持たない。
#ifndef HIKARI_GEOMETRY_AUX_OPAQUE
#define HIKARI_GEOMETRY_AUX_OPAQUE 0
#endif
static const bool kGeometryAuxOpaque = HIKARI_GEOMETRY_AUX_OPAQUE != 0;

#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
struct PSOutput
{
    float4 packedSurface : SV_Target0;
    uint2 featureMask : SV_Target1;
    float4 outlineStyle : SV_Target2;
};
#endif

#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
PSOutput main(PSInput input)
#else
float4 main(PSInput input) : SV_TARGET
#endif
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
#if !HIKARI_GEOMETRY_AUX_OPAQUE
    HikariApplyShellFade(input.shellLayer, input.position.xy);
#endif
    const float3 geometricNormal =
        (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u && !input.isFrontFace
            ? -normalize(input.normalWS) : normalize(input.normalWS);
#if HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_ALL
    const bool terrainSurface = HikariSurfaceHasTerrainSurface(input.surfaceFeatureFlags);
#else
    // 分割 PSO では AS が担当面だけを渡す。
    static const bool terrainSurface = HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_TERRAIN;
#endif
    // 筆触の殻：forward と同じ関数と入力で殻の被覆を確定する（地形は殻を持たない）。
    const bool painterly = !kGeometryAuxOpaque && !terrainSurface && HikariIsPainterlyShellMaterial(materialData);
    HikariPainterlyStroke stroke = (HikariPainterlyStroke)0;
    if (painterly)
    {
        stroke = HikariSamplePainterlyStroke(materialData, gLinearWrap, input.uv, input.shellLayer);
        HikariApplyPainterlyShellCoverage(materialData, input.shellLayer, input.position.xy, gTimeParams.w,
            geometricNormal, HikariCameraViewDirection(input.worldPosWS), stroke.mask);
    }
    HikariMaterialExpressionResult materialExpression = (HikariMaterialExpressionResult)0;
#if HIKARI_SURFACE_PARTITION != HIKARI_SURFACE_PARTITION_TERRAIN
    if (!kGeometryAuxOpaque && !terrainSurface && !painterly &&
        materialData.materialProgramIndex != HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
    {
        HikariMaterialExpressionContext expressionContext =
            HikariBuildMaterialExpressionContext(
                input.uv, input.uv1, gTimeParams, input.worldPosWS,
                geometricNormal, input.tangentWS, gCameraPos.xyz,
                input.position, gScreenParams.zw);
        expressionContext.shellLayer = input.shellLayer;
        materialExpression = HikariEvaluateMaterialExpression(
            materialData, expressionContext, gLinearWrap);
    }
#endif
    if (!kGeometryAuxOpaque && (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0)
    {
        float4 albedo = HikariResolveMaterialBaseColor(materialData, input.uv, input.uv1, gLinearWrap);
        float alphaClipThreshold = materialData.pbrParams.w;
        HikariApplyMaterialExpressionCoverage(materialExpression, albedo, alphaClipThreshold);
        if (albedo.a < alphaClipThreshold) discard;
    }
    if (!kGeometryAuxOpaque && HikariSurfaceHasMaterialFx(input.surfaceFeatureFlags))
    {
        const HikariSurfaceGpuSceneInstance instance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariApplyStaticMaterialFxCoverage(
            input.worldPosWS, instance.fxUser[0], instance.fxUser[1]);
    }
#if HIKARI_SURFACE_PARTITION != HIKARI_SURFACE_PARTITION_GENERAL
    if (terrainSurface)
    {
        const HikariSurfaceGpuSceneInstance terrainInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariClipTerrainCoverage(
            HikariLoadTerrainSurfaceParams(
                terrainInstance.surfaceFeatureDataIndex),
            input.uv1);
    }
#endif

    HikariMaterialPbrInputs pbrInputs = HikariResolveMaterialPbrInputs(
        materialData, input.uv, input.uv1, gLinearWrap);
    HikariApplyMaterialExpressionPbr(materialExpression, pbrInputs);
    if (painterly)
    {
        HikariApplyPainterlyShellPbr(
            input.shellLayer, pbrInputs.metallic, pbrInputs.roughness, pbrInputs.specularFactor);
    }
    float roughness = pbrInputs.roughness;
    float metallic = pbrInputs.metallic;

    float3 n = normalize(geometricNormal);
    if (terrainSurface)
    {
#if HIKARI_SURFACE_PARTITION != HIKARI_SURFACE_PARTITION_GENERAL
        const HikariSurfaceGpuSceneInstance terrainInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        n = HikariResolveTerrainSurfaceAuxNormal(
            materialData,
            HikariLoadTerrainSurfaceParams(
                terrainInstance.surfaceFeatureDataIndex),
            input.worldPosWS,
            input.uv1,
            input.normalWS,
            input.tangentWS,
            gLinearClamp);
#endif
    }
    else
    {
        n = HikariResolveSurfaceShadingNormal(
            materialData,
            geometricNormal,
            input.tangentWS,
            input.uv,
            input.uv1,
            gLinearWrap);
    }
    if (!terrainSurface)
    {
        n = HikariResolveMaterialExpressionNormal(
            materialExpression, materialData.normalScale, geometricNormal, input.tangentWS, n);
        if (painterly)
        {
            n = HikariApplyPainterlyStrokeNormal(n, geometricNormal, input.tangentWS, stroke.normalTS);
        }
    }
    // SSR / AO が見る法線と粗さを、受光の前にデカールを適用した可視の材質（StaticPS）と揃える。
    {
        HikariDecalSurface decalSurface;
        decalSurface.albedo = 0.0f.xxx;
        decalSurface.normal = n;
        decalSurface.roughness = roughness;
        decalSurface.metallic = metallic;
        HikariApplyLocalDecals(input.worldPosWS, input.position.xy, HikariCameraViewDepth(input.worldPosWS),
            geometricNormal, ddx(input.worldPosWS), ddy(input.worldPosWS), decalSurface);
        n = decalSurface.normal;
        roughness = decalSurface.roughness;
        metallic = decalSurface.metallic;
    }
    if (!terrainSurface)
    {
        roughness = HikariFilterPerceptualRoughness(roughness, ddx(n), ddy(n));
    }
    const float4 packedSurface = HikariPackSceneSurface(n, roughness, metallic);
#if defined(HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK)
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
    PSOutput output;
    output.packedSurface = packedSurface;
    const float4 faceHairParameters = HikariLoadMaterialProgramParameter(
        materialData,
        10u,
        float4(0.04f, 0.0f, 0.42f, 3.0f));
    output.featureMask = uint2(
        HikariPackStylizedFaceHairParameters(
            HikariBuildStylizedFeatureMask(instance.materialProgramFlags),
            faceHairParameters.z,
            faceHairParameters.w),
        HikariResolveStylizedGroupKey(
            instance.postGroupMask,
            instance.objectIdLow,
            instance.objectIdHigh));
    output.outlineStyle = HikariEncodeStylizedOutlineStyle(
        HikariLoadMaterialProgramParameter(
            materialData,
            19u,
            float4(0.025f, 0.030f, 0.045f, 2.0f)));
    return output;
#else
    return packedSurface;
#endif
}
