#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpressionSurface.hlsli"
#include "Include/Material/HIKARI_StylizedMaterial.hlsli"
#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"

#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
#include "Include/Forward/HIKARI_SurfaceFeatureShading.hlsli"
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    // VS/MS 側の TEXCOORD スロットと一致させ、PSO リンク時の再割り当てを避ける。
    // forwardMeta のビットレイアウトは HIKARI_ForwardVertexMeta.hlsli を参照。
    nointerpolation uint forwardMeta : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugSurfaceId : TEXCOORD6;
    float3 characterRightWS : TEXCOORD8;
    float3 characterForwardWS : TEXCOORD9;
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
    // 水面と沙地が共有する surface feature 出力。TEXCOORD11 を一つに
    // 統合し、フラグに応じてどちらかの変形データを読む。
    float3 surfaceFeatureData : TEXCOORD11;
    bool isFrontFace : SV_IsFrontFace;
};

#include "Include/Surface/HIKARI_SandSurfaceShading.hlsli"
#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"

#define HIKARI_WATER_EMBEDDED 1
#include "Render3D_WaterSurfacePS.hlsl"
#undef HIKARI_WATER_EMBEDDED

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
};

float4 HikariShadeForwardWithReflections(PSInput input,
    out float4 fallbackSpecular, out float4 specularResponse)
#else
float4 main(PSInput input) : SV_TARGET
#endif
{
#if HIKARI_FORWARD_REFLECTION_CAPTURE
    // 未対応の早期 return も必ず無効値を書き、背後の面の反射を残さない。
    fallbackSpecular = 0.0f.xxxx;
    specularResponse = 0.0f.xxxx;
#endif
    const bool neutralClay =
        gDebugView == HIKARI_DEBUG_VIEW_NEUTRAL_CLAY;
    const bool sandSurface =
        HikariSurfaceHasSandSurface(input.surfaceFeatureFlags);
#if HIKARI_FORWARD_COST_MODE_STATIC != 255
    static const uint forwardCostMode = HIKARI_FORWARD_COST_MODE_STATIC;
#else
    const uint forwardCostMode = (uint)(gForwardCostMode + 0.5f);
#endif
    const bool costNoShadow =
        forwardCostMode == HIKARI_FORWARD_COST_NO_SHADOW;
    // Neutral Clay も authored coverage の外側を描いてはいけない。
    // 深度/GeometryAux と同じ clip を先に適用し、debug 表示だけが
    // 四角い terrain chunk を復活させる不一致を防ぐ。
    if (neutralClay && sandSurface)
    {
        const HikariSurfaceGpuSceneInstance sandInstance =
            HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
        HikariClipSandCoverage(
            HikariLoadSandSurfaceParamsFromDataIndex(
                sandInstance.surfaceFeatureDataIndex),
            input.uv1);
    }
    if (!neutralClay && sandSurface)
    {
        const HikariMeshObjectData sandObjectData =
            HikariGetMeshObjectDataForPixel(
                input.objectDataIndex,
                input.surfaceGpuSceneIndex);
        return HikariShadeSandSurface(
            input,
            sandObjectData,
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
    const float3 v = toCamera / max(distanceToCamera, 0.00001f);
    const float3 l = normalize(-gDirectionalDir.xyz);

    HikariMaterialExpressionResult materialExpression =
        (HikariMaterialExpressionResult)0;
    if (!neutralClay &&
        materialData.materialProgramIndex !=
            HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
    {
        const HikariMaterialExpressionContext expressionContext =
            HikariBuildMaterialExpressionContext(
                input.uv, input.uv1, gTimeParams, input.worldPosWS,
                geometricNormal, input.tangentWS, gCameraPos.xyz,
                input.position, gScreenParams.zw);
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
            gDirectionalColor.rgb * gDirectionalIntensity);
        if ((materialData.instanceFlags &
             HIKARI_PROGRAM_FLAG_PREMULTIPLIED_ALPHA_INPUT) != 0u)
        {
            baseColorSample.rgb = baseColorSample.a > 1.0e-5f
                ? baseColorSample.rgb / baseColorSample.a
                : 0.0f.xxx;
        }
        albedo *= baseColorSample;
    }
    float alphaClipThreshold = materialData.pbrParams.w;
    HikariApplyMaterialExpressionCoverage(materialExpression, albedo, alphaClipThreshold);
    if (!neutralClay &&
        (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 &&
        albedo.a < alphaClipThreshold)
    {
        discard;
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
        if (materialData.materialProgramIndex ==
            HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
        {
        float3 direct =
            HikariEvaluateDirectionalPbr(
                pbrContext,
                l,
                gDirectionalColor.rgb,
                gDirectionalIntensity,
                gDirectionalDir.w);
        direct *= shadowVisibility;

        float3 pointDirect = AccumulateLocalLightsPbr(
            pbrContext,
            input.worldPosWS,
            input.position.xy);

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

        shadedColor = direct + pointDirect + (ambient.diffuse + ambient.specular);
#if HIKARI_FORWARD_REFLECTION_CAPTURE
        if ((gDebugView == 0u || gDebugView == 26u || gDebugView == 27u) && !neutralClay)
        {
            fallbackSpecular = float4(ambient.specular, 1.0f);
            specularResponse = float4(ambient.specularResponse, 1.0f);
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
                    gDirectionalColor.rgb * gDirectionalIntensity,
                    HikariStylizedShadowFactor(shadowVisibility),
                    pbrIndirect);
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
        materialData.materialProgramIndex == HIKARI_STANDARD_PBR_MATERIAL_PROGRAM;
#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
    reflectionSupported = reflectionSupported && !hasStaticMaterialFx;
#endif
    // 既存の非単位 alpha 合成は複数面を含むため、単一表面の置換対象にしない。
    reflectionSupported = reflectionSupported && albedo.a == 1.0f;
    HikariAttenuateReflectionContribution(fallbackSpecular, specularResponse,
        1.0f - HikariForwardFogFactor(input.worldPosWS), reflectionSupported);
#endif
    return float4(
        alphaBlended ? finalColor * albedo.a : finalColor,
        albedo.a);
}

#if HIKARI_FORWARD_REFLECTION_CAPTURE
HikariForwardReflectionOutput main(PSInput input)
{
    HikariForwardReflectionOutput result;
    result.color = HikariShadeForwardWithReflections(input, result.fallbackSpecular, result.specularResponse);
    return result;
}
#endif
