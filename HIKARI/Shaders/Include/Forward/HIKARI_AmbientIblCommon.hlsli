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
    float specularFactor, float3 normal, float3 view, float clearCoat = 0.0f, float clearCoatRoughness = 0.04f)
{
    HikariPbrShadingContext context = HikariBuildPbrShadingContext(
        baseColor, metallic, roughness, specularColor, specularFactor, normal, view);
    HikariApplyEnvironmentBrdf(context);
    HikariApplyClearCoat(context, clearCoat, clearCoatRoughness);
    return context;
}

// skyVisibility は空の見える割合（動的 GI と光照贴图が知る。他は 1）。surfaceLightmap.rgb は焼き込んだ E/pi、
// a は焼き込んだ空の見える割合。sampleProbes を偽にすると動的 GI と焼き込み probe を読まず、固定の環境光と空の拡散光
// だけにする（前向きの診断モードが probe の読み出し費用を測る）。
float3 HikariResolveSurfaceDiffuseIrradiance(float3 worldPos, float3 geometricNormal, float3 n, float3 v,
    float4 surfaceLightmap, float surfaceLightmapWeight, out float skyVisibility, bool sampleProbes = true)
{
    skyVisibility = 1.0f;
    const float weight = saturate(surfaceLightmapWeight);
    // Lightmap の全面 coverage は下位 provider を置換する。有効な黒も維持する。光照贴图の面も空の反射を遮る
    // （以前は 1 のままで、室内の床や壁に空が映り、Cycles より 2–5 割明るく青かった）。
    [branch]
    if (weight >= 1.0f)
    {
        skyVisibility = saturate(surfaceLightmap.a);
        return surfaceLightmap.rgb;
    }
    float4 probes = 0.0f.xxxx;
    [branch] if (sampleProbes)
        probes = HikariResolveLightProbeDiffuse(worldPos, geometricNormal, n, v, skyVisibility);
    skyVisibility = lerp(skyVisibility, saturate(surfaceLightmap.a), weight);
    [branch] if (probes.a >= 1.0f)
        return lerp(probes.rgb, surfaceLightmap.rgb, weight);
    // 固定の環境光（中立の確認表示だけが使う）と空の拡散光。
    float3 diffuse = gAmbientColor.rgb * gAmbientIntensity + HikariEvaluateSkyDiffuseIrradiance(n);
    diffuse = probes.rgb + diffuse * (1.0f - probes.a);
    return lerp(diffuse, surfaceLightmap.rgb, weight);
}

// sampleProbes / sampleReflection を偽にすると、probe の読み出し（上の関数）と環境の鏡面反射を省く。前向きの診断モードが
// それぞれの費用を測るための引数で、通常の描画は既定の真のまま。
HikariAmbientIblLobes HikariEvaluateAmbientIblLobes(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos,
    float3 geometricNormal,
    float4 surfaceLightmap,
    float surfaceLightmapWeight,
    bool sampleProbes = true,
    bool sampleReflection = true)
{
    const float3 baseColor = context.baseColor;
    const float roughness = context.roughness;
    const float3 n = context.normal;
    const float3 v = context.view;
    const float3 kD = context.diffuseEnergyWeight;

    float skyVisibility;
    const float3 diffuseIbl = HikariResolveSurfaceDiffuseIrradiance(
        worldPos, geometricNormal, n, v, surfaceLightmap, surfaceLightmapWeight, skyVisibility, sampleProbes);
    float3 diffuse =
        kD * baseColor * diffuseIbl;

    // 空が遮られた所では空の反射の代わりに周囲の照り返し（拡散の E/pi）を映す（Lagarde の鏡面遮蔽）。
    const float skyOcclusion = saturate(pow(context.ndotView + skyVisibility, exp2(-16.0f * roughness - 1.0f)) -
        1.0f + skyVisibility);
    float3 specular = 0.0f.xxx;
    [branch] if (sampleReflection)
        specular = HikariSampleEnvironmentReflection(
            worldPos, normalize(reflect(-v, n)), roughness, skyOcclusion, diffuseIbl) * context.environmentSpecularResponse;
    float3 specularResponse = context.environmentSpecularResponse;
    if (context.clearCoat > 0.0f)
    {
        // コートの環境反射。基底の応答は HikariApplyClearCoat で既にコートの Fresnel 分だけ減っている。
        const float2 coatBrdf = HikariAnalyticEnvBrdf(context.ndotView, context.clearCoatRoughness);
        const float coatResponse = (HIKARI_CLEAR_COAT_F0 * coatBrdf.x + coatBrdf.y) * context.clearCoat;
        [branch] if (sampleReflection)
            specular += HikariSampleEnvironmentReflection(worldPos, normalize(reflect(-v, n)),
                context.clearCoatRoughness, skyOcclusion, diffuseIbl) * coatResponse;
        specularResponse += coatResponse.xxx;
    }

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
    result.specularResponse = specularResponse * specularAo;
    return result;
}

float3 HikariEvaluateAmbientIbl(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos,
    float3 geometricNormal,
    float4 surfaceLightmap,
    float surfaceLightmapWeight)
{
    const HikariAmbientIblLobes lobes = HikariEvaluateAmbientIblLobes(
        context, occlusion, screenAo, worldPos, geometricNormal, surfaceLightmap, surfaceLightmapWeight);
    return lobes.diffuse + lobes.specular;
}

#endif
