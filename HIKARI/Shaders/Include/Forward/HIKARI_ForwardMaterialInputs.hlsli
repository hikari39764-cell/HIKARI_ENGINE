#ifndef HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED
#define HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED

void ResolveSurfaceLightmap(
    uint surfaceGpuSceneIndex,
    float2 uv1,
    out float3 irradiance,
    out float weight)
{
    irradiance = 0.0f.xxx;
    weight = 0.0f;
#if HIKARI_SURFACE_GPU_SCENE_CONSUME
    if (!HikariCanConsumeSurfaceGpuSceneForPixel() ||
        gSurfaceLightmapEnabled < 0.5f)
    {
        return;
    }
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    const uint descriptorIndex = instance.lightmapTextureDescriptorIndex;
    if (descriptorIndex == 0xffffffffu ||
        descriptorIndex >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT)
    {
        return;
    }
    const float2 lightmapUv =
        uv1 * instance.lightmapUvScaleBias.xy +
        instance.lightmapUvScaleBias.zw;
    irradiance = gMaterialTexturePool[
        NonUniformResourceIndex(descriptorIndex)].Sample(
            gLinearClamp,
            saturate(lightmapUv)).rgb;
    weight = 1.0f;
#endif
}

float3 AccumulateLocalLights(
    float3 normalWS,
    float3 worldPosWS,
    float3 viewDir,
    float distanceToCamera,
    float2 pixelPosition)
{
    float3 sum = 0.0f.xxx;
    const uint2 cluster = HikariResolveLocalLightCluster(
        pixelPosition,
        distanceToCamera);
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const uint i = gHikariLocalLightClusterIndices[cluster.x + slot];
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[i],
                worldPosWS,
                lightSample))
        {
            continue;
        }
        const float ndotl =
            saturate(dot(normalWS, lightSample.directionToLight));
        const float3 h = normalize(lightSample.directionToLight + viewDir);
        const float spec =
            pow(saturate(dot(normalWS, h)), gSpecularParams.y);
        sum += lightSample.radiance *
            (ndotl + gSpecularParams.x * spec);
    }
    return sum;
}

float3 AccumulateLocalLightsPbr(
    HikariPbrShadingContext pbrContext,
    float3 worldPosWS,
    float distanceToCamera,
    float2 pixelPosition)
{
    float3 sum = 0.0f.xxx;
    const uint2 cluster = HikariResolveLocalLightCluster(
        pixelPosition,
        distanceToCamera);
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const uint i = gHikariLocalLightClusterIndices[cluster.x + slot];
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[i],
                worldPosWS,
                lightSample))
        {
            continue;
        }
        sum += HikariEvaluateDirectPbr(
            pbrContext,
            lightSample.directionToLight,
            lightSample.radiance,
            1.0f);
    }
    return sum;
}

float3 ResolveEmissive(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1)
{
    float3 emissive = materialData.emissiveFactor.rgb;
    if (materialData.hasEmissiveTexture != 0)
    {
        const float2 emissiveUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_EMISSIVE,
            uv0,
            uv1);
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
        const float2 uv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_SPECULAR,
            uv0,
            uv1);
        specularFactor *= HikariSampleMaterialTexture(
            materialData.specularTextureDescriptorIndex,
            gLinearWrap,
            uv,
            1.0f.xxxx).a;
    }
    if (materialData.hasSpecularColorTexture != 0)
    {
        const float2 uv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_SPECULAR_COLOR,
            uv0,
            uv1);
        specularColor *= HikariSampleMaterialTexture(
            materialData.specularColorTextureDescriptorIndex,
            gLinearWrap,
            uv,
            1.0f.xxxx).rgb;
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
        const float2 uv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS,
            uv0,
            uv1);
        const float4 mr = HikariSampleMaterialTexture(
            materialData.metallicRoughnessTextureDescriptorIndex,
            gLinearWrap,
            uv,
            1.0f.xxxx);
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
        metallic = saturate(metallic * mr.b);
    }
    if (materialData.hasOcclusionTexture != 0)
    {
        const float2 uv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_OCCLUSION,
            uv0,
            uv1);
        const float ao = HikariSampleMaterialTexture(
            materialData.occlusionTextureDescriptorIndex,
            gLinearWrap,
            uv,
            1.0f.xxxx).r;
        occlusion = lerp(
            1.0f,
            ao,
            saturate(materialData.pbrParams.z));
    }
}

bool HikariNeedsSpecularGlossCompatibility(
    HikariMeshMaterialData materialData,
    float metallic)
{
    if ((materialData.materialFlags & MATERIAL_SPECULAR_GLOSS_COMPAT) != 0)
    {
        return true;
    }
    return materialData.hasSpecularColorTexture != 0 &&
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
    const bool alphaMasked =
        (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0;
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
    const float dist = length(gCameraPos.xyz - worldPosWS) * 0.8f;
    const float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(
        fogFactor * max(0.0f, gFogColorDensity.a) * dist);
    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

#endif
