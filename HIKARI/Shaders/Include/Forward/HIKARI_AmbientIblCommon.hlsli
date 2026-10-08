#ifndef HIKARI_AMBIENT_IBL_COMMON_HLSLI
#define HIKARI_AMBIENT_IBL_COMMON_HLSLI

struct HikariAmbientIblLobes
{
    float3 diffuse;
    float3 specular;
    float3 specularResponse;
};

HikariPbrShadingContext HikariBuildForwardPbrShadingContext(
    float3 baseColor, float metallic, float roughness, float3 specularColor,
    float specularFactor, float3 normal, float3 view)
{
    HikariPbrShadingContext context = HikariBuildPbrShadingContext(
        baseColor, metallic, roughness, specularColor, specularFactor, normal, view);
    HikariApplyEnvironmentBrdf(context);
    return context;
}

// skyVisibility は空の見える割合（動的 GI だけが知る。他は 1）。
float3 HikariResolveSurfaceDiffuseIrradiance(float3 worldPos, float3 n, float3 v,
    float3 surfaceLightmapIrradiance, float surfaceLightmapWeight, out float skyVisibility)
{
    skyVisibility = 1.0f;
    const float weight = saturate(surfaceLightmapWeight);
    // Lightmap の全面 coverage は下位 provider を置換する。有効な黒も維持する。
    [branch]
    if (weight >= 1.0f) return surfaceLightmapIrradiance;
    const float4 probes = HikariResolveLightProbeDiffuse(worldPos, n, v, skyVisibility);
    [branch] if (probes.a >= 1.0f)
        return lerp(probes.rgb, surfaceLightmapIrradiance, weight);
    // 固定の環境光（中立の確認表示だけが使う）と空の拡散光。
    float3 diffuse = gAmbientColor.rgb * gAmbientIntensity + HikariEvaluateSkyDiffuseIrradiance(n);
    diffuse = probes.rgb + diffuse * (1.0f - probes.a);
    return lerp(diffuse, surfaceLightmapIrradiance, weight);
}

HikariAmbientIblLobes HikariEvaluateAmbientIblLobes(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos,
    float3 surfaceLightmapIrradiance,
    float surfaceLightmapWeight)
{
    const float3 baseColor = context.baseColor;
    const float roughness = context.roughness;
    const float3 n = context.normal;
    const float3 v = context.view;
    const float3 kD = context.diffuseEnergyWeight;

    float skyVisibility;
    const float3 diffuseIbl = HikariResolveSurfaceDiffuseIrradiance(
        worldPos, n, v, surfaceLightmapIrradiance, surfaceLightmapWeight, skyVisibility);
    float3 diffuse =
        kD * baseColor * diffuseIbl;

    // 空が遮られた所では空の反射の代わりに周囲の照り返し（拡散の E/pi）を映す（Lagarde の鏡面遮蔽）。
    const float skyOcclusion = saturate(pow(context.ndotView + skyVisibility, exp2(-16.0f * roughness - 1.0f)) -
        1.0f + skyVisibility);
    const float3 specular = HikariSampleEnvironmentReflection(
        worldPos, normalize(reflect(-v, n)), roughness, skyOcclusion, diffuseIbl) * context.environmentSpecularResponse;

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo =
        materialAo *
        lerp(
            1.0f,
            ssao,
            saturate(gSsaoDiffuseStrength));
    float specularAo = lerp(
        1.0f,
        materialAo * ssao,
        saturate(roughness * roughness) *
            saturate(gSsaoSpecularStrength));
    HikariAmbientIblLobes result;
    result.diffuse = diffuse * diffuseAo;
    result.specular = specular * specularAo;
    result.specularResponse = context.environmentSpecularResponse * specularAo;
    return result;
}

float3 HikariEvaluateAmbientIbl(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos,
    float3 surfaceLightmapIrradiance,
    float surfaceLightmapWeight)
{
    const HikariAmbientIblLobes lobes = HikariEvaluateAmbientIblLobes(
        context, occlusion, screenAo, worldPos, surfaceLightmapIrradiance, surfaceLightmapWeight);
    return lobes.diffuse + lobes.specular;
}

#endif
