#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_StylizedMaterial.hlsli"

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
#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
#endif
};

#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"

#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
#define HIKARI_WATER_EMBEDDED 1
#include "Render3D_FxWaterPS.hlsl"
#undef HIKARI_WATER_EMBEDDED
#endif

float4 main(PSInput input) : SV_TARGET
{
    const bool neutralClay =
        gDebugView == HIKARI_DEBUG_VIEW_NEUTRAL_CLAY;
#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
    if (!neutralClay &&
        HikariSurfaceHasWaterMaterialFx(input.surfaceFeatureFlags))
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
    hasStaticMaterialFx = HikariObjectDataHasMaterialFx(surfaceFeatureData);
#endif
#endif

    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
#if HIKARI_FORWARD_COST_MODE_STATIC != 255
    static const uint forwardCostMode = HIKARI_FORWARD_COST_MODE_STATIC;
#else
    const uint forwardCostMode = (uint)(gForwardCostMode + 0.5f);
#endif
    const bool costAlbedoOnly = forwardCostMode == HIKARI_FORWARD_COST_ALBEDO_ONLY;
    const bool costNoNormalMap = forwardCostMode == HIKARI_FORWARD_COST_NO_NORMAL_MAP;
    const bool costNoShadow = forwardCostMode == HIKARI_FORWARD_COST_NO_SHADOW;
    const bool costNoSsao = forwardCostMode == HIKARI_FORWARD_COST_NO_SSAO;
    const bool costNoMaterialExtras =
        forwardCostMode == HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS;

    float4 albedo = neutralClay
        ? float4(0.72f, 0.74f, 0.77f, 1.0f)
        : materialData.baseColor;
    if (!neutralClay && materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if (!neutralClay &&
        (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 &&
        albedo.a < materialData.pbrParams.w)
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

    float3 geometricNormal = normalize(input.normalWS);
    float3 n = neutralClay
        ? geometricNormal
        : costNoNormalMap
        ? geometricNormal
        : HikariResolveSurfaceShadingNormal(
            materialData,
            input.normalWS,
            input.tangentWS,
            input.uv,
            input.uv1,
            gLinearWrap);
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

    float3 l = normalize(-gDirectionalDir.xyz);
    const float3 toCamera =
        gCameraPos.xyz - input.worldPosWS;
    const float distanceToCamera = length(toCamera);
    float3 v =
        toCamera /
        max(distanceToCamera, 0.00001f);
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

    float3 finalColor = shadedColor + emissive;
    finalColor = ApplyFog(finalColor, input.worldPosWS);
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
    return float4(finalColor, albedo.a);
}
