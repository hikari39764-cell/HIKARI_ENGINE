#ifndef HIKARI_PBR_COMMON_HLSLI
#define HIKARI_PBR_COMMON_HLSLI

static const float HIKARI_PI = 3.14159265359f;

float HikariFilterPerceptualRoughness(
    float perceptualRoughness,
    float3 normalDx,
    float3 normalDy)
{
    // 法線分散を GGX の alpha^2 に加える。知覚的粗さへ直接足すと
    // 曲面の質感が失われるため、フィルタ幅を制限してから四乗根に戻す。
    // Tokuyoshi/Kaplanyan の等方的法線フィルタ。分散は回転に依存しない。
    const float roughness = clamp(perceptualRoughness, 0.04f, 1.0f);
    const float alpha = roughness * roughness;
    const float variance = 0.15f * (dot(normalDx, normalDx) + dot(normalDy, normalDy));
    const float kernel = min(2.0f * variance, 0.18f);
    return sqrt(sqrt(saturate(alpha * alpha + kernel)));
}

float2 HikariAnalyticEnvBrdf(float ndotv, float roughness)
{
    // Karis の環境 BRDF 近似。入力は知覚的粗さと F0 用の scale/bias。
    // https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
    const float4 r = saturate(roughness) * float4(-1.0f, -0.0275f, -0.572f, 0.022f)
        + float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float grazing = min(r.x * r.x, exp2(-9.28f * saturate(ndotv))) * r.x + r.y;
    const float2 fit = max(float2(-1.04f, 1.04f) * grazing + r.zw, 0.0f.xx);
    // 近似の掠射角端点でも、単位反射体の入射エネルギーを超えない。
    return fit / max(1.0f, fit.x + fit.y);
}

float HikariPow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 HikariFresnelSchlick(float cosTheta, float3 F0, float F90)
{
    return F0 + (F90.xxx - F0) * HikariPow5(1.0f - saturate(cosTheta));
}

float HikariDielectricF0FromIor(float incidentIor, float transmittedIor)
{
    const float denominator = max(
        incidentIor + transmittedIor,
        1.0e-4f);
    const float ratio =
        (incidentIor - transmittedIor) / denominator;
    return ratio * ratio;
}

float3 HikariSpecularF0(
    float3 baseColor,
    float metallic,
    float3 specularColor,
    float specularFactor)
{
    // 色係数は 1 を超えてよい。反射率を制限してから層の強度を掛ける。
    float3 dielectricF0 = saturate(0.04f.xxx * max(0.0f.xxx, specularColor));
    dielectricF0 = saturate(dielectricF0 * max(0.0f, specularFactor));
    return lerp(dielectricF0, saturate(baseColor), saturate(metallic));
}

struct HikariPbrShadingContext
{
    float3 baseColor;
    float metallic;
    float3 specularF0;
    float specularF90;
    float roughness;
    float3 normal;
    float ndotView;
    float3 view;
    float distributionAlphaSquared;
    float visibilityViewRoot;
    float3 specularEnergyCompensation;
    float3 environmentSpecularResponse;
    float3 diffuseEnergyWeight;
};

void HikariSetPbrEnvironmentBrdf(inout HikariPbrShadingContext context, float2 envBrdf)
{
    if (!all(isfinite(envBrdf)) || envBrdf.x + envBrdf.y < 1.0e-4f)
        envBrdf = HikariAnalyticEnvBrdf(context.ndotView, context.roughness);
    envBrdf = max(envBrdf, 0.0f.xx);
    envBrdf /= max(1.0f, envBrdf.x + envBrdf.y);
    const float singleScatterEnergy = max(envBrdf.x + envBrdf.y, 1.0e-4f);
    // Turquin の GGX 多重散乱近似。LUT は A/B なので単位 Fresnel の積分は A+B。
    context.specularEnergyCompensation = 1.0f.xxx + context.specularF0 * (1.0f/singleScatterEnergy-1.0f);
    context.environmentSpecularResponse = saturate(
        (context.specularF0 * envBrdf.x + context.specularF90 * envBrdf.y) * context.specularEnergyCompensation);
    // 指向半球反射率の残りを Lambert 拡散へ割り当て、白色炉内で二重にエネルギーを使わない。
    const float reflectedEnergy = max(context.environmentSpecularResponse.r,
        max(context.environmentSpecularResponse.g, context.environmentSpecularResponse.b));
    context.diffuseEnergyWeight = (1.0f-reflectedEnergy).xxx * (1.0f-context.metallic);
}

HikariPbrShadingContext HikariBuildPbrShadingContext(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float3 normal,
    float3 view)
{
    HikariPbrShadingContext context;
    context.baseColor = baseColor;
    context.metallic = metallic;
    context.specularF0 = HikariSpecularF0(
        baseColor,
        metallic,
        specularColor,
        specularFactor);
    // Specular=0 は全角度で誘電体の反射を止める。金属と水の物理 F90 は 1。
    context.specularF90 = lerp(saturate(specularFactor), 1.0f, saturate(metallic));
    context.roughness = HikariFilterPerceptualRoughness(
        roughness, ddx(normal), ddy(normal));
    context.normal = normal;
    context.view = view;

    const float ndotView = saturate(dot(normal, view));
    context.ndotView = ndotView;
    const float alpha = context.roughness * context.roughness;
    context.distributionAlphaSquared = alpha * alpha;
    context.visibilityViewRoot = sqrt(context.distributionAlphaSquared +
        (1.0f-context.distributionAlphaSquared) * ndotView * ndotView);
    HikariSetPbrEnvironmentBrdf(context, HikariAnalyticEnvBrdf(ndotView, context.roughness));
    return context;
}

float HikariDistributionGGX(
    HikariPbrShadingContext context,
    float3 halfVector)
{
    float ndoth = saturate(
        dot(context.normal, halfVector));
    if (ndoth <= 0.0f) return 0.0f;
    // |N x H|^2 は 1-NdotH^2 の差し引き誤差を避ける。
    // 分母を大きく切り上げると、滑らかな GGX ローブの頂点が消失する。
    const float3 crossNormalHalf = cross(context.normal, halfVector);
    const float denom = dot(crossNormalHalf, crossNormalHalf) +
        ndoth * ndoth * context.distributionAlphaSquared;
    return context.distributionAlphaSquared /
        max(HIKARI_PI * denom * denom, 1.0e-12f);
}

float HikariVisibilitySmithGGX(HikariPbrShadingContext context, float ndotLight)
{
    // Height-correlated Smith。直接光と IBL Baker は同じ GGX 遮蔽を積分する。
    const float lightRoot = sqrt(context.distributionAlphaSquared +
        (1.0f-context.distributionAlphaSquared) * ndotLight * ndotLight);
    return 0.5f / max(ndotLight * context.visibilityViewRoot + context.ndotView * lightRoot, 1.0e-8f);
}

struct HikariDirectPbrLobes
{
    float3 diffuse;
    float3 specular;
};

HikariDirectPbrLobes HikariEvaluateDirectPbrLobes(
    HikariPbrShadingContext context,
    float3 l,
    float3 radiance)
{
    HikariDirectPbrLobes result = (HikariDirectPbrLobes)0;
    float ndotl = saturate(
        dot(context.normal, l));

    if (ndotl <= 0.0f || context.ndotView <= 0.0f)
    {
        return result;
    }

    float3 h = normalize(context.view + l);
    float3 F = HikariFresnelSchlick(
        saturate(dot(h, context.view)),
        context.specularF0,
        context.specularF90);
    float D = HikariDistributionGGX(
        context,
        h);
    float3 specular = D * HikariVisibilitySmithGGX(context, ndotl) * F * context.specularEnergyCompensation;
    float3 diffuse =
        context.diffuseEnergyWeight * context.baseColor / HIKARI_PI;

    result.diffuse = diffuse * radiance * ndotl;
    result.specular = specular * radiance * ndotl;
    return result;
}

float3 HikariEvaluateDirectPbr(
    HikariPbrShadingContext context, float3 l, float3 lightColor, float lightIntensity)
{
    const HikariDirectPbrLobes lobes = HikariEvaluateDirectPbrLobes(context, l, lightColor * lightIntensity);
    return lobes.diffuse + lobes.specular;
}

#endif
