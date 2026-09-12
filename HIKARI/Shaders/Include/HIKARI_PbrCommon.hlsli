#ifndef HIKARI_PBR_COMMON_HLSLI
#define HIKARI_PBR_COMMON_HLSLI

static const float HIKARI_PI = 3.14159265359f;

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

float3 HikariFresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f.xxx - F0) * HikariPow5(1.0f - saturate(cosTheta));
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

// 環境光 (IBL) 用 Fresnel。粗い表面はミクロ面の遮蔽により掠射角でも反射が
// 立ち上がらないため、F90 を粗さで減衰させる (Karis の ambient fresnel)。
// これが無いと粗い表面 (植生など) の縁が空の色で白く洗われる。
float3 HikariFresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 f90 = max((1.0f - roughness).xxx, F0);
    return F0 + (f90 - F0) * HikariPow5(1.0f - saturate(cosTheta));
}

float3 HikariSpecularF0(
    float3 baseColor,
    float metallic,
    float3 specularColor,
    float specularFactor)
{
    float3 dielectricF0 = saturate(0.04f.xxx * max(0.0f, specularFactor) * max(0.0f.xxx, specularColor));
    return lerp(dielectricF0, baseColor, saturate(metallic));
}

struct HikariPbrShadingContext
{
    float3 baseColor;
    float metallic;
    float3 specularF0;
    float roughness;
    float3 normal;
    float ndotView;
    float3 view;
    float distributionAlphaSquared;
    float geometryK;
    float geometryView;
};

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
    // Specular AA (Karis/Filament style): estimate local surface curvature
    // from shading-normal derivatives and raise perceptual roughness so
    // undersampled highlights and metal edges do not alias or shimmer.
    const float3 normalDerivative = fwidth(normal);
    const float curvature = 2.0f * length(normalDerivative);
    context.roughness = clamp(max(roughness, curvature), 0.04f, 1.0f);
    context.normal = normal;
    context.view = view;

    const float ndotView = saturate(dot(normal, view));
    context.ndotView = ndotView;
    const float alpha = context.roughness * context.roughness;
    context.distributionAlphaSquared = alpha * alpha;
    const float geometryRoughness = context.roughness + 1.0f;
    context.geometryK =
        (geometryRoughness * geometryRoughness) / 8.0f;
    context.geometryView =
        ndotView /
        max(
            ndotView * (1.0f - context.geometryK) +
                context.geometryK,
            0.00001f);
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

float HikariGeometrySchlickGGX(
    float ndotDirection,
    float geometryK)
{
    return ndotDirection /
        max(
            ndotDirection * (1.0f - geometryK) +
                geometryK,
            0.00001f);
}

float3 HikariEvaluateDirectPbr(
    HikariPbrShadingContext context,
    float3 l,
    float3 lightColor,
    float lightIntensity)
{
    float ndotl = saturate(
        dot(context.normal, l));

    if (ndotl <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 h = normalize(context.view + l);
    float3 F = HikariFresnelSchlick(
        saturate(dot(h, context.view)),
        context.specularF0);
    float D = HikariDistributionGGX(
        context,
        h);
    float G =
        context.geometryView *
        HikariGeometrySchlickGGX(
            ndotl,
            context.geometryK);

    float3 numerator = D * G * F;
    float denominator = max(
        4.0f * max(context.ndotView, 0.0001f) * ndotl,
        0.0001f);
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD =
        (1.0f.xxx - kS) *
        (1.0f - context.metallic);
    float3 diffuse =
        kD * context.baseColor / HIKARI_PI;

    return (diffuse + specular) *
        lightColor *
        lightIntensity *
        ndotl;
}

#endif
