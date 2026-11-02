#ifndef HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED
#define HIKARI_FORWARD_MATERIAL_INPUTS_INCLUDED

// irradiance.rgb は焼き込んだ間接拡散の E/pi、a は余弦で重みづけた空の見える割合（鏡面の遮蔽に使う）。
void ResolveSurfaceLightmap(
    uint surfaceGpuSceneIndex,
    float2 uv1,
    out float4 irradiance,
    out float weight)
{
    irradiance = float4(0.0f, 0.0f, 0.0f, 1.0f);
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
            saturate(lightmapUv));
    weight = 1.0f;
#endif
}

#include "../Lighting/HIKARI_SurfaceDirectLighting.hlsli"

#include "../Material/HIKARI_MaterialPbrInputs.hlsli"

// 体積霧が無い時の解析的な高さ霧。gFogParams = {有効, 開始距離, 基準高さ, 高さ減衰 [1/m]}、
// gFogColorDensity.a は基準高さでの消散係数 [1/m]。
float HikariForwardFogFactor(float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return 0.0f;
    }
    const float distance = max(length(gCameraPos.xyz - worldPosWS) - gFogParams.y, 0.0f);
    const float falloff = max(gFogParams.w, 0.0f);
    const float h0 = max(gCameraPos.y - gFogParams.z, 0.0f);
    const float h1 = max(worldPosWS.y - gFogParams.z, 0.0f);
    // 視線上の密度の平均（指数の高さ分布を区間で積分）。
    float heightScale = exp(-falloff * h0);
    if (falloff > 1.0e-5f && abs(h1 - h0) > 1.0e-3f)
    {
        heightScale = (exp(-falloff * h0) - exp(-falloff * h1)) / (falloff * (h1 - h0));
    }
    return 1.0f - exp(-max(gFogColorDensity.a, 0.0f) * heightScale * distance);
}

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }
    return lerp(color, gFogColorDensity.rgb, HikariForwardFogFactor(worldPosWS));
}

#endif
