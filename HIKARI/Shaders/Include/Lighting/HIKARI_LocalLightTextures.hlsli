#ifndef HIKARI_LOCAL_LIGHT_TEXTURES_INCLUDED
#define HIKARI_LOCAL_LIGHT_TEXTURES_INCLUDED

// 点・スポットの配光（IES）とスポットのクッキー。include する側が gMaterialTexturePool、
// HIKARI_MATERIAL_TEXTURE_POOL_COUNT、gLinearClamp を宣言する。
// 光源の記録: directionInnerCos.xyz がローカル +Z、shapeTangent.xyz がローカル +X、
// shapeTangent.w が配光の記述子番号 + 1 + (クッキーの記述子番号 + 1) * 4096 の整数値（0 は無し）。
#include "HIKARI_LocalLightContract.hlsli"

// 配光の表（ASSETS::LIGHTING::kIesProfileVerticalSamples / kIesProfileHorizontalSamples）。横が鉛直角 0..180°
// （+Z から）、縦が水平角 0..360°（+X から +Y へ）、R に最大値で割った光度の平方根を持つ。
static const float2 HIKARI_IES_TABLE_SIZE = float2(64.0f, 32.0f);

float3 HikariSampleLocalLightTexture(uint encodedIndex, float2 uv)
{
    const uint index = encodedIndex - 1u;
    if (index >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT)
        return 1.0f.xxx;
    return gMaterialTexturePool[NonUniformResourceIndex(index)].SampleLevel(gLinearClamp, uv, 0.0f).rgb;
}

// 光源から受け手への向きの配光とクッキーの倍率。どちらも持たない光源は 1。
float3 HikariLocalLightTextureFactor(HikariLocalLightGpuData light, float3 directionToLight)
{
    const uint type = light.kind & 0xffu;
    const uint textures = (uint)light.shapeTangent.w;
    if (textures == 0u || (type != HIKARI_LOCAL_LIGHT_TYPE_POINT && type != HIKARI_LOCAL_LIGHT_TYPE_SPOT))
        return 1.0f.xxx;
    const float3 forward = light.directionInnerCos.xyz;
    const float3 right = light.shapeTangent.xyz;
    const float3 fromLight = -directionToLight;
    const float3 local = float3(dot(fromLight, right), dot(fromLight, cross(forward, right)), dot(fromLight, forward));
    float3 factor = 1.0f.xxx;
    const uint iesProfile = textures & 4095u;
    if (iesProfile != 0u)
    {
        const float vertical = acos(clamp(local.z, -1.0f, 1.0f)) * 0.31830988f;
        const float horizontal = frac(atan2(local.y, local.x) * 0.15915494f + 1.0f);
        const float2 uv = (float2(vertical, horizontal) * (HIKARI_IES_TABLE_SIZE - 1.0f) + 0.5f) /
            HIKARI_IES_TABLE_SIZE;
        const float encoded = HikariSampleLocalLightTexture(iesProfile, uv).r;
        factor *= encoded * encoded;
    }
    const uint cookie = textures >> 12u;
    if (cookie != 0u && type == HIKARI_LOCAL_LIGHT_TYPE_SPOT)
    {
        // 外側の円錐の縁がテクスチャの縁。右手系で +Z を向く光源の後ろから見て、テクスチャの右が -X、上が +Y
        // （投影が鏡像にならない）。
        if (local.z <= 1e-4f)
            return 0.0f.xxx;
        const float outerCos = max(light.outerConeCos, 1e-4f);
        const float outerTangent = sqrt(saturate(1.0f - outerCos * outerCos)) / outerCos;
        const float2 uv = 0.5f - 0.5f * local.xy / (local.z * outerTangent);
        factor *= HikariSampleLocalLightTexture(cookie, saturate(uv));
    }
    return factor;
}

// 解決済みの照明に配光とクッキーを掛ける。
void HikariApplyLocalLightTextures(HikariLocalLightGpuData light, inout HikariLocalLightSample lightSample)
{
    const float3 factor = HikariLocalLightTextureFactor(light, lightSample.directionToLight);
    lightSample.radiance *= factor;
    lightSample.specularRadiance *= factor;
}

#endif
