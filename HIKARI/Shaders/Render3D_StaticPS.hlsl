#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpressionSurface.hlsli"
#include "Include/Material/HIKARI_StylizedMaterial.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#include "Include/Surface/HIKARI_MaterialShell.hlsli"

#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
#include "Include/Forward/HIKARI_SurfaceFeatureShading.hlsli"
#endif

typedef HikariForwardPixelInput PSInput;

#if HIKARI_SURFACE_PARTITION != HIKARI_SURFACE_PARTITION_GENERAL
#include "Include/Surface/HIKARI_TerrainSurfaceShading.hlsli"
#endif
#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"

// 水面は depth-aware/transparent の全経路 PSO だけが描く。
#if HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_ALL
#define HIKARI_WATER_EMBEDDED 1
#include "Render3D_WaterSurfacePS.hlsl"
#undef HIKARI_WATER_EMBEDDED
#endif

#ifndef HIKARI_FORWARD_REFLECTION_CAPTURE
#define HIKARI_FORWARD_REFLECTION_CAPTURE 0
#endif

#if HIKARI_FORWARD_REFLECTION_CAPTURE
#include "Include/Surface/HIKARI_SceneReflectionContribution.hlsli"
struct HikariForwardReflectionOutput
{
    float4 color : SV_Target0;
    float4 fallbackSpecular : SV_Target1;
    float4 specularResponse : SV_Target2;
    float4 reflectionMaterial : SV_Target3; // RGB=F0, A=F90 (減衰前の材質)
};

float4 HikariShadeForwardWithReflections(PSInput input,
    out float4 fallbackSpecular, out float4 specularResponse, out float4 reflectionMaterial)
#else
float4 main(PSInput input) : SV_TARGET
#endif
{
#if HIKARI_FORWARD_REFLECTION_CAPTURE
    // 未対応の早期 return も必ず無効値を書き、背後の面の反射を残さない。
    fallbackSpecular = 0.0f.xxxx;
    specularResponse = 0.0f.xxxx;
    reflectionMaterial = 0.0f.xxxx;
#endif
#if HIKARI_FORWARD_COST_MODE_STATIC != 255
    static const uint forwardCostMode = HIKARI_FORWARD_COST_MODE_STATIC;
#else
    const uint forwardCostMode = (uint)(gForwardCostMode + 0.5f);
#endif
    HikariApplyShellFade(input.shellLayer, input.position.xy);
    const bool costNoShadow =
        forwardCostMode == HIKARI_FORWARD_COST_NO_SHADOW;
#if HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_TERRAIN
    // 地形専用 PSO。AS が地形以外の可視範囲を除外している。
    // debug view 中は CPU が全経路 PSO へ切り替えるため、ここでは通常着色だけを持つ。
    return HikariShadeTerrainSurface(
        input,
        HikariGetMeshObjectDataForPixel(
            input.objectDataIndex,
            input.surfaceGpuSceneIndex),
        input.surfaceFeatureData,
        costNoShadow);
#else
    const bool neutralClay =
        gDebugView == HIKARI_DEBUG_VIEW_NEUTRAL_CLAY;
#if HIKARI_SURFACE_PARTITION == HIKARI_SURFACE_PARTITION_ALL
    const bool terrainSurface =
        HikariSurfaceHasTerrainSurface(input.surfaceFeatureFlags);
    // Neutral Clay も authored coverage の外側を描いてはいけない。
    // 深度/GeometryAux と同じ clip を先に適用し、debug 表示だけが
    // 四角い terrain chunk を復活させる不一致を防ぐ。
    if (neutralClay && terrainSurface)
    {
        const HikariSurfaceGpuSceneInstance terrainInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariClipTerrainCoverage(
            HikariLoadTerrainSurfaceParams(
                terrainInstance.surfaceFeatureDataIndex),
            input.uv1);
    }
    if (!neutralClay && terrainSurface)
    {
        const HikariMeshObjectData terrainObjectData =
            HikariGetMeshObjectDataForPixel(
                input.objectDataIndex,
                input.surfaceGpuSceneIndex);
        return HikariShadeTerrainSurface(
            input,
            terrainObjectData,
            input.surfaceFeatureData,
            costNoShadow);
    }
    if (!neutralClay &&
        HikariSurfaceHasWaterSurface(input.surfaceFeatureFlags))
    {
        const HikariMeshObjectData waterObjectData =
            HikariGetMeshObjectDataForPixel(
                input.objectDataIndex,
                input.surfaceGpuSceneIndex);
        return HikariShadeWaterSurface(input, waterObjectData);
    }
#endif
#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
    HikariMeshObjectData surfaceFeatureData = (HikariMeshObjectData)0;
    bool hasStaticMaterialFx = false;
#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
    hasStaticMaterialFx = HikariSurfaceHasMaterialFx(input.surfaceFeatureFlags);
    if (hasStaticMaterialFx)
    {
        surfaceFeatureData = HikariGetMeshObjectDataForPixel(
            input.objectDataIndex,
            input.surfaceGpuSceneIndex);
    }
#else
    surfaceFeatureData = HikariGetMeshObjectDataForPixel(
        input.objectDataIndex,
        input.surfaceGpuSceneIndex);
    hasStaticMaterialFx =
        HikariObjectDataHasMaterialFx(surfaceFeatureData);
#endif
#endif

    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    const bool doubleSidedBackFace =
        (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u &&
        !input.isFrontFace;
    const float3 surfaceGeometricNormal = normalize(input.normalWS);
    const float3 geometricNormal =
        doubleSidedBackFace
            ? -surfaceGeometricNormal
            : surfaceGeometricNormal;
    const bool costAlbedoOnly = forwardCostMode == HIKARI_FORWARD_COST_ALBEDO_ONLY;
    const bool costNoNormalMap = forwardCostMode == HIKARI_FORWARD_COST_NO_NORMAL_MAP;
    const bool costNoSsao = forwardCostMode == HIKARI_FORWARD_COST_NO_SSAO;
    const bool costNoMaterialExtras =
        forwardCostMode == HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS;

    const float3 toCamera = gCameraPos.xyz - input.worldPosWS;
    const float distanceToCamera = length(toCamera);
    const float3 v = HikariCameraViewDirection(input.worldPosWS);
    const float3 l = normalize(-gDirectionalDir.xyz);
    const float cloudSolarVisibility=costAlbedoOnly || (materialData.materialFlags & HIKARI_MATERIAL_FLAG_UNLIT)!=0u
        ? 1.0f : HikariForwardCloudSolarVisibility(input.worldPosWS);

    HikariMaterialExpressionResult materialExpression =
        (HikariMaterialExpressionResult)0;
    if (!neutralClay &&
        materialData.materialProgramIndex !=
            HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
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

    float4 albedo = neutralClay
        ? float4(0.72f, 0.74f, 0.77f, 1.0f)
        : materialData.baseColor;
    if (!neutralClay && materialData.hasBaseColorTexture != 0)
    {
        float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        const float2 eyeSurfaceUv = baseColorUv;
        baseColorUv = HikariResolveEyeLayerUv(
            materialData,
            baseColorUv,
            geometricNormal,
            input.tangentWS,
            v);
        baseColorUv = HikariResolveCorneaHighlightUv(
            materialData,
            baseColorUv,
            geometricNormal,
            input.tangentWS,
            v,
            l);
        float4 baseColorSample = HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
        baseColorSample = HikariResolveEyeLayerSample(
            materialData,
            baseColorSample);
        baseColorSample = HikariResolveCorneaHighlightSample(
            materialData,
            baseColorSample,
            eyeSurfaceUv,
            geometricNormal,
            input.tangentWS,
            v,
            l,
            gDirectionalColor.rgb * (gDirectionalIntensity * cloudSolarVisibility));
        baseColorSample = HikariResolveMaterialSourceColor(materialData, baseColorSample);
        albedo *= baseColorSample;
    }
    float alphaClipThreshold = materialData.pbrParams.w;
    materialExpression.baseColor = HikariResolveMaterialSourceColor(materialData, materialExpression.baseColor);
    HikariApplyMaterialExpressionCoverage(materialExpression, albedo, alphaClipThreshold);
    if (!neutralClay &&
        ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 ||
         ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_AFTERIMAGE) != 0u &&
          (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_ALPHA_MASK) != 0u)) &&
        albedo.a < alphaClipThreshold)
    {
        discard;
    }

    // Frozen render-only silhouettes share the production geometry, skinning,
    // coverage and transparent depth state. They do not shade the source RGB.
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_AFTERIMAGE) != 0u)
    {
        float coverage = (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_ALPHA_BLEND) != 0u ? albedo.a : 1.0f;
        float alpha = saturate(coverage * materialData.emissiveFactor.w);
        return float4(max(materialData.emissiveFactor.rgb, 0.0f) * alpha, alpha);
    }

    if (gDebugView == 4 || costAlbedoOnly)
    {
        return float4(albedo.rgb, albedo.a);
    }
    if (gDebugView == 11)
    {
        float sceneDepth = gSceneDepthTex.Load(int3(int2(input.position.xy), 0)).r;
        return float4(sceneDepth.xxx, albedo.a);
    }
    if (gDebugView == 12)
    {
        float2 screenUv = input.position.xy * gScreenParams.zw;
        return float4(gSceneColorTex.Sample(gLinearWrap, saturate(screenUv)).rgb, albedo.a);
    }

    float4 geometryDebugColor;
    if (HikariTryResolveGeometryDebugView(
        gDebugView,
        HikariForwardVertexMetaClusterId(input.forwardMeta),
        input.debugSurfaceId,
        HikariForwardVertexMetaLodIndex(input.forwardMeta),
        HikariForwardVertexMetaDrawBucket(input.forwardMeta),
        albedo.a,
        geometryDebugColor))
    {
        return geometryDebugColor;
    }

    float3 n = neutralClay
        ? geometricNormal
        : costNoNormalMap
        ? geometricNormal
        : HikariResolveSurfaceShadingNormal(
            materialData,
            geometricNormal,
            input.tangentWS,
            input.uv,
            input.uv1,
            gLinearWrap);
    n = HikariResolveMaterialExpressionNormal(
        materialExpression, materialData.normalScale, geometricNormal, input.tangentWS, n);
    if (gDebugView == 1)
    {
        return float4(normalize(n) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 2)
    {
        return float4(normalize(input.tangentWS.xyz) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 23)
    {
        float2 providerWeights =
            HikariEvaluateReflectionProviderWeights(
                input.worldPosWS);
        return float4(
            providerWeights.x,
            providerWeights.y,
            0.0f,
            albedo.a);
    }
    if (gDebugView == 24)
    {
        float2 providerWeights =
            HikariEvaluateIrradianceProviderWeights(
                input.worldPosWS);
        return float4(
            providerWeights.x,
            providerWeights.y,
            0.0f,
            albedo.a);
    }
    if (gDebugView == 25)
    {
        float2 reflectionWeights =
            HikariEvaluateReflectionProviderWeights(
                input.worldPosWS);
        float2 irradianceWeights =
            HikariEvaluateIrradianceProviderWeights(
                input.worldPosWS);
        float reflectionFallback =
            HikariEvaluateProviderFallbackWeight(
                reflectionWeights);
        float irradianceFallback =
            HikariEvaluateProviderFallbackWeight(
                irradianceWeights);
        return float4(
            reflectionFallback,
            irradianceFallback,
            max(
                reflectionFallback,
                irradianceFallback),
            albedo.a);
    }

    float ndotl = saturate(dot(n, l));
    if (gDebugView == 9)
    {
        return float4(ndotl.xxx, albedo.a);
    }

    HikariMaterialPbrInputs pbrInputs;
    if (neutralClay)
    {
        pbrInputs.metallic = 0.0f;
        pbrInputs.roughness = 1.0f;
        pbrInputs.occlusion = 1.0f;
        pbrInputs.specularColor = 0.0f.xxx;
        pbrInputs.specularFactor = 0.0f;
    }
    else if (costNoMaterialExtras)
    {
        pbrInputs.metallic = saturate(materialData.pbrParams.x);
        pbrInputs.roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
        pbrInputs.occlusion = 1.0f;
        pbrInputs.specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
        pbrInputs.specularFactor = max(0.0f, materialData.specularParams.w);
    }
    else
    {
        pbrInputs = HikariResolveMaterialPbrInputs(
            materialData, input.uv, input.uv1, gLinearWrap);
    }
    HikariApplyMaterialExpressionPbr(materialExpression, pbrInputs);
    const float metallic = pbrInputs.metallic;
    const float roughness = pbrInputs.roughness;
    const float occlusion = pbrInputs.occlusion;
    const float3 specularColor = pbrInputs.specularColor;
    const float specularFactor = pbrInputs.specularFactor;
    if (gDebugView == 5)
    {
        return float4(roughness.xxx, albedo.a);
    }
    if (gDebugView == 6)
    {
        return float4(metallic.xxx, albedo.a);
    }
    if (gDebugView == 7)
    {
        return float4(occlusion.xxx, albedo.a);
    }

    float screenAo = 1.0f;
    if (!costNoSsao && gSsaoEnabled > 0.5f)
    {
        screenAo = gSsaoTex.Load(int3(int2(input.position.xy), 0)).r;
    }
    float shadowVisibility = costNoShadow
        ? 1.0f
        : SampleDirectionalVisibility(
            input.worldPosWS,
            geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta));
    if (gDebugView == 8)
    {
        return float4(shadowVisibility.xxx, albedo.a);
    }

    float3 emissive = (!neutralClay &&
        !costNoMaterialExtras &&
        (materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
        ? HikariResolveMaterialEmissive(materialData, input.uv, input.uv1, gLinearWrap)
        : 0.0f.xxx;
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_EMISSIVE) != 0u)
    {
        emissive = max(0.0f.xxx, materialExpression.emissive);
    }
    if (gDebugView == 10)
    {
        return float4(emissive, albedo.a);
    }

    float3 shadedColor = albedo.rgb;
    if (neutralClay ||
        (materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
        HikariPbrShadingContext pbrContext =
            HikariBuildForwardPbrShadingContext(
                albedo.rgb,
                metallic,
                roughness,
                specularColor,
                specularFactor,
                n,
                v);
        if (HikariMaterialUsesStandardPbrLighting(materialData))
        {
        HikariDirectPbrLobes directional =
            HikariEvaluateDirectionalPbrLobes(
                pbrContext,
                l,
                gDirectionalColor.rgb * (gDirectionalIntensity * cloudSolarVisibility),
                gDirectionalDir.w);
        directional.diffuse *= shadowVisibility;
        directional.specular *= shadowVisibility;

        const HikariDirectPbrLobes local = AccumulateLocalLightPbrLobes(
            pbrContext,
            input.worldPosWS,
            input.position.xy, geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta));

        float3 surfaceLightmapIrradiance = 0.0f.xxx;
        float surfaceLightmapWeight = 0.0f;
        ResolveSurfaceLightmap(
            input.surfaceGpuSceneIndex,
            input.uv1,
            surfaceLightmapIrradiance,
            surfaceLightmapWeight);
        if (gDebugView == 28)
        {
            return float4(
                surfaceLightmapWeight > 0.0f
                    ? surfaceLightmapIrradiance
                    : float3(1.0f, 0.0f, 1.0f),
                albedo.a);
        }

        const HikariAmbientIblLobes ambient = HikariEvaluateAmbientIblLobes(
            pbrContext,
            occlusion,
            screenAo,
            input.worldPosWS,
            surfaceLightmapIrradiance,
            surfaceLightmapWeight);

        shadedColor = directional.diffuse + directional.specular +
            local.diffuse + local.specular + ambient.diffuse + ambient.specular;
        // Lit と同じローブを分離し、別のデバッグ用 BRDF を持たない。
        // Post/Fog/SSR を混ぜずに材質と各光源の寄与を調べるための HDR 出力。
        if (gDebugView == HIKARI_DEBUG_VIEW_DIRECT_DIFFUSE)
            return float4(directional.diffuse + local.diffuse, albedo.a);
        if (gDebugView == HIKARI_DEBUG_VIEW_DIRECTIONAL_SPECULAR)
            return float4(directional.specular, albedo.a);
        if (gDebugView == HIKARI_DEBUG_VIEW_LOCAL_SPECULAR)
            return float4(local.specular, albedo.a);
        if (gDebugView == HIKARI_DEBUG_VIEW_INDIRECT_DIFFUSE)
            return float4(ambient.diffuse, albedo.a);
        if (gDebugView == HIKARI_DEBUG_VIEW_INDIRECT_SPECULAR)
            return float4(ambient.specular, albedo.a);
#if HIKARI_FORWARD_REFLECTION_CAPTURE
        if ((gDebugView == 0u || gDebugView == 26u || gDebugView == 27u) && !neutralClay)
        {
            fallbackSpecular = float4(ambient.specular, 1.0f);
            specularResponse = float4(ambient.specularResponse, 1.0f);
            reflectionMaterial = float4(pbrContext.specularF0, pbrContext.specularF90);
        }
#endif
        }
        else
        {
            float3 surfaceLightmapIrradiance = 0.0f.xxx;
            float surfaceLightmapWeight = 0.0f;
            ResolveSurfaceLightmap(
                input.surfaceGpuSceneIndex,
                input.uv1,
                surfaceLightmapIrradiance,
                surfaceLightmapWeight);
            if (gDebugView == 28)
            {
                return float4(
                    surfaceLightmapWeight > 0.0f
                        ? surfaceLightmapIrradiance
                        : float3(1.0f, 0.0f, 1.0f),
                    albedo.a);
            }
            const float3 pbrIndirect = HikariEvaluateAmbientIbl(
                pbrContext,
                occlusion,
                screenAo,
                input.worldPosWS,
                surfaceLightmapIrradiance,
                surfaceLightmapWeight);
            HikariMaterialSurfaceData surface;
            surface.baseColor = albedo.rgb;
            surface.opacity = albedo.a;
            surface.normalWS = n;
            surface.geometricNormalWS = surfaceGeometricNormal;
            surface.metallic = metallic;
            surface.viewWS = v;
            surface.roughness = roughness;
            surface.specularColor = specularColor;
            surface.specularFactor = specularFactor;
            surface.emissive = emissive;
            surface.occlusion = occlusion;
            const HikariMaterialLightingResponse response =
                HikariEvaluateStylizedMaterial(
                    materialData,
                    surface,
                    pbrContext,
                    input.worldPosWS,
                    input.position.xy,
                    input.tangentWS,
                    input.characterRightWS,
                    input.characterForwardWS,
                    input.uv,
                    !doubleSidedBackFace,
                    l,
                    gDirectionalColor.rgb * (gDirectionalIntensity * cloudSolarVisibility),
                    shadowVisibility,
                    pbrIndirect, HikariForwardVertexMetaReceiveShadow(input.forwardMeta));
            shadedColor = response.directDiffuse +
                response.directSpecular +
                response.indirect +
                response.additive;
        }
    }
    if (gDebugView == 3)
    {
        return float4(ApplyFog(shadedColor, input.worldPosWS), albedo.a);
    }

    const bool transparentEyeHighlight =
        HikariIsTransparentEyeHighlight(materialData) &&
        ((materialData.materialFlags & MATERIAL_ALPHA_BLEND) != 0u ||
         (materialData.instanceFlags &
          HIKARI_PROGRAM_FLAG_ALPHA_BLEND) != 0u);
    // Eye highlight artwork is a compositing layer, not a second PBR shell.
    // Keeping it in straight color here lets the shared transparent return
    // premultiply exactly once below.
    float3 finalColor = transparentEyeHighlight
        ? albedo.rgb
        : ApplyFog(shadedColor + emissive, input.worldPosWS);
#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
    if (!neutralClay &&
        !costNoMaterialExtras &&
        hasStaticMaterialFx)
    {
        finalColor = HikariApplyStaticMaterialFx(
            finalColor,
            input.worldPosWS,
            geometricNormal,
            n,
            v,
            input.position,
            gTimeParams.x,
            surfaceFeatureData);
    }
#endif
    const bool alphaBlended =
        (materialData.materialFlags & MATERIAL_ALPHA_BLEND) != 0u ||
        (materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_ALPHA_BLEND) != 0u;
#if HIKARI_FORWARD_REFLECTION_CAPTURE
    bool reflectionSupported = !neutralClay && (gDebugView == 0u || gDebugView == 26u || gDebugView == 27u) && !alphaBlended &&
        (materialData.materialFlags & MATERIAL_UNLIT) == 0u &&
        HikariMaterialUsesStandardPbrLighting(materialData);
#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
    reflectionSupported = reflectionSupported && !hasStaticMaterialFx;
#endif
    // 既存の非単位 alpha 合成は複数面を含むため、単一表面の置換対象にしない。
    reflectionSupported = reflectionSupported && albedo.a == 1.0f;
    HikariAttenuateReflectionContribution(fallbackSpecular, specularResponse,
        1.0f - HikariForwardFogFactor(input.worldPosWS), reflectionSupported);
    if (!HikariHasReflectionContribution(fallbackSpecular, specularResponse)) reflectionMaterial = 0.0f.xxxx;
#endif
    return float4(
        alphaBlended ? finalColor * albedo.a : finalColor,
        albedo.a);
#endif
}

#if HIKARI_FORWARD_REFLECTION_CAPTURE
HikariForwardReflectionOutput main(PSInput input)
{
    HikariForwardReflectionOutput result;
    result.color = HikariShadeForwardWithReflections(input, result.fallbackSpecular, result.specularResponse, result.reflectionMaterial);
    return result;
}
#endif
