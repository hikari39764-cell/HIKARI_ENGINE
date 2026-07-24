#include "Include/Forward/HIKARI_ForwardCommon.hlsli"

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
#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7;
#endif
};

float3 ResolveShadingNormal(
    HikariMeshMaterialData materialData,
    float3 normalWS,
    float4 tangentWS,
    float2 uv0,
    float2 uv1)
{
    float3 n = normalize(normalWS);
    if (materialData.hasNormalTexture == 0)
    {
        return n;
    }

    float3 t = tangentWS.xyz;
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }

    t = normalize(t);
    t = t - n * dot(n, t);
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }
    t = normalize(t);
    float3 b = normalize(cross(n, t) * tangentWS.w);

    const float2 normalUv =
        HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_NORMAL, uv0, uv1);
    float3 normalTS =
        HikariSampleMaterialTexture(
            materialData.normalTextureDescriptorIndex,
            gLinearWrap,
            normalUv,
            float4(0.5f, 0.5f, 1.0f, 1.0f)).xyz * 2.0f - 1.0f;
    normalTS.xy *= materialData.normalScale;
    normalTS = normalize(normalTS);
    return normalize(normalTS.x * t + normalTS.y * b + normalTS.z * n);
}

float3 AccumulatePointLight(float3 normalWS, float3 worldPosWS, float3 viewDir)
{
    float3 sum = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount)
        {
            break;
        }

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);
        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? (toLight / dist) : float3(0.0f, 1.0f, 0.0f);

        float atten = saturate(1.0f - dist / range);
        atten *= atten;

        float ndotl = saturate(dot(normalWS, l));
        float3 h = normalize(l + viewDir);
        float spec = pow(saturate(dot(normalWS, h)), gSpecularParams.y);

        float3 color = gPointLightColorIntensity[i].rgb;
        float intensity = gPointLightColorIntensity[i].w;
        sum += color * (atten * intensity) * (ndotl + gSpecularParams.x * spec);
    }
    return sum;
}

float3 AccumulatePointLightPbr(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float3 normalWS,
    float3 worldPosWS,
    float3 viewDir)
{
    float3 sum = 0.0f.xxx;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount)
        {
            break;
        }

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);
        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? (toLight / dist) : float3(0.0f, 1.0f, 0.0f);

        float atten = saturate(1.0f - dist / range);
        atten *= atten;

        float3 color = gPointLightColorIntensity[i].rgb;
        float intensity = gPointLightColorIntensity[i].w;

        sum += HikariEvaluateDirectPbr(
            baseColor,
            metallic,
            roughness,
            specularColor,
            specularFactor,
            normalWS,
            viewDir,
            l,
            color,
            intensity * atten);
    }

    return sum;
}

float3 ResolveEmissive(HikariMeshMaterialData materialData, float2 uv0, float2 uv1)
{
    float3 emissive = materialData.emissiveFactor.rgb;
    if (materialData.hasEmissiveTexture != 0)
    {
        const float2 emissiveUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_EMISSIVE, uv0, uv1);
        emissive *= HikariSampleMaterialTexture(
            materialData.emissiveTextureDescriptorIndex,
            gLinearWrap,
            emissiveUv,
            float4(0.0f, 0.0f, 0.0f, 1.0f)).rgb;
    }
    return emissive * materialData.emissiveFactor.a;
}

void ResolveSpecularInputs(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    out float3 specularColor,
    out float specularFactor)
{
    specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
    specularFactor = max(0.0f, materialData.specularParams.w);

    if (materialData.hasSpecularTexture != 0)
    {
        const float2 specularUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_SPECULAR, uv0, uv1);
        specularFactor *= HikariSampleMaterialTexture(
            materialData.specularTextureDescriptorIndex,
            gLinearWrap,
            specularUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).a;
    }

    if (materialData.hasSpecularColorTexture != 0)
    {
        const float2 specularColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_SPECULAR_COLOR, uv0, uv1);
        specularColor *= HikariSampleMaterialTexture(
            materialData.specularColorTextureDescriptorIndex,
            gLinearWrap,
            specularColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).rgb;
    }
}

void ResolvePbrInputs(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    out float metallic,
    out float roughness,
    out float occlusion)
{
    metallic = saturate(materialData.pbrParams.x);
    roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    occlusion = 1.0f;

    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        const float2 metallicRoughnessUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS, uv0, uv1);
        float4 mr = HikariSampleMaterialTexture(
            materialData.metallicRoughnessTextureDescriptorIndex,
            gLinearWrap,
            metallicRoughnessUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
        metallic = saturate(metallic * mr.b);
    }

    if (materialData.hasOcclusionTexture != 0)
    {
        const float2 occlusionUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_OCCLUSION, uv0, uv1);
        float ao = HikariSampleMaterialTexture(
            materialData.occlusionTextureDescriptorIndex,
            gLinearWrap,
            occlusionUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).r;
        occlusion = lerp(1.0f, ao, saturate(materialData.pbrParams.z));
    }
}

bool HikariNeedsSpecularGlossCompatibility(HikariMeshMaterialData materialData, float metallic)
{
    if ((materialData.materialFlags & MATERIAL_SPECULAR_GLOSS_COMPAT) != 0)
    {
        return true;
    }

    return
        materialData.hasSpecularColorTexture != 0 &&
        materialData.hasMetallicRoughnessTexture == 0 &&
        metallic < 0.001f;
}

void ApplyPbrMaterialCompatibility(
    HikariMeshMaterialData materialData,
    float metallic,
    inout float roughness,
    inout float3 specularColor,
    inout float specularFactor)
{
    const bool alphaMasked = (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0;
    if (alphaMasked)
    {
        roughness = max(roughness, 0.72f);
        specularFactor *= 0.75f;
    }

    if (!HikariNeedsSpecularGlossCompatibility(materialData, metallic))
    {
        return;
    }

    roughness = max(roughness, alphaMasked ? 0.86f : 0.72f);
    specularFactor *= alphaMasked ? 0.45f : 0.65f;
    specularColor *= alphaMasked ? 0.70f : 0.85f;
}

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }

    float dist = length(gCameraPos.xyz - worldPosWS) * 0.8f;
    float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(fogFactor * max(0.0f, gFogColorDensity.a) * dist);
    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
#define HIKARI_WATER_EMBEDDED 1
#include "Render3D_FxWaterPS.hlsl"
#undef HIKARI_WATER_EMBEDDED
#endif

float4 main(PSInput input) : SV_TARGET
{
#if defined(HIKARI_FORWARD_MESHLET_SURFACE_FEATURES) && HIKARI_FORWARD_MESHLET_SURFACE_FEATURES
    if (HikariSurfaceHasWaterMaterialFx(input.surfaceFeatureFlags))
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

    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < materialData.pbrParams.w)
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
    float3 n = costNoNormalMap
        ? geometricNormal
        : ResolveShadingNormal(materialData, input.normalWS, input.tangentWS, input.uv, input.uv1);
    if (gDebugView == 1)
    {
        return float4(normalize(n) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 2)
    {
        return float4(normalize(input.tangentWS.xyz) * 0.5f + 0.5f, albedo.a);
    }

    float3 l = normalize(-gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
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
    if (costNoMaterialExtras)
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

    float3 emissive = (!costNoMaterialExtras && (materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
        ? ResolveEmissive(materialData, input.uv, input.uv1)
        : 0.0f.xxx;
    if (gDebugView == 10)
    {
        return float4(emissive, albedo.a);
    }

    float3 shadedColor = albedo.rgb;
    if ((materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
#if HIKARI_USE_COOK_TORRANCE_PBR
        float3 direct =
            HikariEvaluateDirectPbr(
                albedo.rgb,
                metallic,
                roughness,
                specularColor,
                specularFactor,
                n,
                v,
                l,
                gDirectionalColor.rgb,
                gDirectionalIntensity);
        direct *= shadowFactor;

        float3 pointDirect = AccumulatePointLightPbr(
            albedo.rgb,
            metallic,
            roughness,
            specularColor,
            specularFactor,
            n,
            input.worldPosWS,
            v);

        float3 ambient = HikariEvaluateAmbientIbl(
            albedo.rgb,
            metallic,
            roughness,
            specularColor,
            specularFactor,
            occlusion,
            screenAo,
            n,
            v,
            input.worldPosWS);

        shadedColor = direct + pointDirect + ambient;
#else
    float specPower = lerp(gSpecularParams.y, 8.0f, roughness);
    float spec = pow(saturate(dot(n, h)), max(1.0f, specPower));

    float3 ambient = gAmbientColor.rgb * gAmbientIntensity * occlusion;
    float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl) * (1.0f - metallic * 0.65f);
    float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec) * lerp(1.0f, 1.8f, metallic);
    float3 pointLightContribution = AccumulatePointLight(n, input.worldPosWS, v);
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
    if (!costNoMaterialExtras && hasStaticMaterialFx)
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
