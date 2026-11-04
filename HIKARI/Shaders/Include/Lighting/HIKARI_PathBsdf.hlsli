#ifndef HIKARI_PATH_BSDF_INCLUDED
#define HIKARI_PATH_BSDF_INCLUDED

#include "../HIKARI_PbrCommon.hlsli"
#include "HIKARI_RaySampling.hlsli"

// 路径の次の向き（路径追跡の参照と光照贴图の烘焙で共用）。拡散（余弦）と鏡面（GGX の見える法線）を反射率の比で
// 選び、標本の重み（BRDF × cos / pdf）を返す。pbr は HikariPbrCommon の面の文脈（法線と視線は当たった面のもの）。
// diffuseOnly は初回の面の間接拡散だけを測る時に鏡面を選ばない。向きが面の下へ潜る時は偽。

float HikariPathLuminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

bool HikariSamplePathBsdf(HikariPbrShadingContext pbr, float3 geometricNormal, bool diffuseOnly, float lobeXi,
    float2 xi, out float3 direction, out float3 weight)
{
    direction = 0.0f.xxx;
    weight = 0.0f.xxx;
    const float3 diffuseAlbedo = pbr.baseColor * pbr.diffuseEnergyWeight;
    const float diffuseWeight = HikariPathLuminance(diffuseAlbedo);
    const float specularWeight = diffuseOnly ? 0.0f : HikariPathLuminance(pbr.environmentSpecularResponse);
    if (diffuseWeight + specularWeight <= 1.0e-6f || pbr.ndotView <= 0.0f)
        return false;
    const float specularProbability = specularWeight / (diffuseWeight + specularWeight);
    if (lobeXi < specularProbability)
    {
        // 見える法線の標本の重みは F・G2/G1（Smith の高さ相関）。
        direction = HikariSampleGgxReflection(pbr.normal, pbr.view, pbr.roughness, xi);
        const float ndotl = dot(pbr.normal, direction);
        if (ndotl <= 0.0f)
            return false;
        const float3 halfVector = normalize(pbr.view + direction);
        const float3 fresnel = HikariFresnelSchlick(dot(pbr.view, halfVector), pbr.specularF0, pbr.specularF90);
        const float shadowing =
            2.0f * ndotl * (pbr.ndotView + pbr.visibilityViewRoot) * HikariVisibilitySmithGGX(pbr, ndotl);
        weight = fresnel * pbr.specularEnergyCompensation * shadowing / specularProbability;
    }
    else
    {
        direction = HikariSampleCosineHemisphere(pbr.normal, xi);
        weight = diffuseAlbedo / (1.0f - specularProbability);
    }
    return dot(direction, geometricNormal) > 0.0f && all(isfinite(weight));
}

#endif
