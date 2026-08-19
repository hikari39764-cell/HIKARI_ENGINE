#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"
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

float4 main(PSInput input) : SV_TARGET
{
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
        HikariMaterialExpressionContext expressionContext;
        expressionContext.uv0 = input.uv;
        expressionContext.uv1 = input.uv1;
        expressionContext.time = gTimeParams;
        expressionContext.worldPosition = input.worldPosWS;
        expressionContext.worldNormal = geometricNormal;
        expressionContext.worldTangent = input.tangentWS;
        expressionContext.viewDirection = v;
        expressionContext.cameraPosition = gCameraPos.xyz;
        expressionContext.screenPosition = float4(
            input.position.xy * gScreenParams.zw,
            input.position.z,
            input.position.w);
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
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_BASE_COLOR) != 0u)
    {
        albedo = materialExpression.baseColor;
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_OPACITY) != 0u)
    {
        albedo.a = saturate(materialExpression.opacity);
    }
    const float alphaClipThreshold =
        (materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_ALPHA_CLIP) != 0u
        ? saturate(materialExpression.alphaClipThreshold)
        : materialData.pbrParams.w;
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
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_NORMAL) != 0u)
    {
        const float normalLengthSquared = dot(
            materialExpression.normalTS,
            materialExpression.normalTS);
        const float3 expressionNormal = normalLengthSquared > 1.0e-10f
            ? materialExpression.normalTS * rsqrt(normalLengthSquared)
            : float3(0.0f, 0.0f, 1.0f);
        // Expression-authored tangent-space normals follow the same strength
        // contract as PBR normal maps: scale XY by normalScale, then rebuild
        // Z so steep inputs never flatten.
        const float2 scaledNormalXY = expressionNormal.xy *
            max(materialData.normalScale, 0.0f);
        const float scaledNormalLengthSquared =
            dot(scaledNormalXY, scaledNormalXY);
        const float3 scaledExpressionNormal =
            scaledNormalLengthSquared < 1.0f
                ? float3(
                    scaledNormalXY,
                    sqrt(1.0f - scaledNormalLengthSquared))
                : normalize(float3(scaledNormalXY, 0.0f));
        n = HikariTransformTangentNormalToWorld(
            scaledExpressionNormal,
            geometricNormal,
            input.tangentWS);
    }
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

#if !HIKARI_USE_COOK_TORRANCE_PBR
    float3 h = normalize(l + v);
#endif

    float ndotl = saturate(dot(n, l));
    if (gDebugView == 9)
    {
        return float4(ndotl.xxx, albedo.a);
    }

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    float3 specularColor = 1.0f.xxx;
    float specularFactor = 1.0f;
    if (neutralClay)
    {
        metallic = 0.0f;
        roughness = 1.0f;
        occlusion = 1.0f;
        specularColor = 0.0f.xxx;
        specularFactor = 0.0f;
    }
    else if (costNoMaterialExtras)
    {
        metallic = saturate(materialData.pbrParams.x);
        roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
        occlusion = 1.0f;
        specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
        specularFactor = max(0.0f, materialData.specularParams.w);
    }
    else
    {
        ResolvePbrInputs(materialData, input.uv, input.uv1, metallic, roughness, occlusion);
        ResolveSpecularInputs(materialData, input.uv, input.uv1, specularColor, specularFactor);
        ApplyPbrMaterialCompatibility(materialData, metallic, roughness, specularColor, specularFactor);
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_METALLIC) != 0u)
    {
        metallic = saturate(materialExpression.metallic);
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_ROUGHNESS) != 0u)
    {
        roughness = clamp(materialExpression.roughness, 0.04f, 1.0f);
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_OCCLUSION) != 0u)
    {
        occlusion = saturate(materialExpression.occlusion);
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_SPECULAR) != 0u)
    {
        specularFactor = max(0.0f, materialExpression.specular);
    }
    if ((materialExpression.outputMask &
         HIKARI_EXPRESSION_OUTPUT_SPECULAR_COLOR) != 0u)
    {
        specularColor = max(0.0f.xxx, materialExpression.specularColor);
    }
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
    float shadowFactor = costNoShadow
        ? 1.0f
        : SampleDirectionalShadow(
            input.worldPosWS,
            geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta));
    if (gDebugView == 8)
    {
        return float4(shadowFactor.xxx, albedo.a);
    }

    float3 emissive = (!neutralClay &&
        !costNoMaterialExtras &&
        (materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
        ? ResolveEmissive(materialData, input.uv, input.uv1)
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
#if HIKARI_USE_COOK_TORRANCE_PBR
        HikariPbrShadingContext pbrContext =
            HikariBuildPbrShadingContext(
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
            HikariEvaluateDirectPbr(
                pbrContext,
                l,
                gDirectionalColor.rgb,
                gDirectionalIntensity);
        direct *= shadowFactor;

        float3 pointDirect = AccumulateLocalLightsPbr(
            pbrContext,
            input.worldPosWS,
            distanceToCamera,
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

        float3 ambient = HikariEvaluateAmbientIbl(
            pbrContext,
            occlusion,
            screenAo,
            input.worldPosWS,
            surfaceLightmapIrradiance,
            surfaceLightmapWeight);

        shadedColor = direct + pointDirect + ambient;
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
                    distanceToCamera,
                    input.position.xy,
                    input.tangentWS,
                    input.characterRightWS,
                    input.characterForwardWS,
                    input.uv,
                    !doubleSidedBackFace,
                    l,
                    gDirectionalColor.rgb * gDirectionalIntensity,
                    shadowFactor,
                    pbrIndirect);
            shadedColor = response.directDiffuse +
                response.directSpecular +
                response.indirect +
                response.additive;
        }
#else
    float specPower = lerp(gSpecularParams.y, 8.0f, roughness);
    float spec = pow(saturate(dot(n, h)), max(1.0f, specPower));

    float3 ambient = gAmbientColor.rgb * gAmbientIntensity * occlusion;
    float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl) * (1.0f - metallic * 0.65f);
    float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec) * lerp(1.0f, 1.8f, metallic);
    float3 pointLightContribution =
        AccumulateLocalLights(
            n,
            input.worldPosWS,
            v,
            distanceToCamera,
            input.position.xy);
    float3 lit = ambient + (diffuse + specular) * shadowFactor + pointLightContribution;
        shadedColor = albedo.rgb * lit;
#endif
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
    return float4(
        alphaBlended ? finalColor * albedo.a : finalColor,
        albedo.a);
}
