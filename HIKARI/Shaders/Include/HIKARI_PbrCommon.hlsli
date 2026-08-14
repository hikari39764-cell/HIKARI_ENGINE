#ifndef HIKARI_PBR_COMMON_HLSLI
#define HIKARI_PBR_COMMON_HLSLI

static const float HIKARI_PI = 3.14159265359f;

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
    context.roughness = saturate(max(roughness, curvature));
    context.normal = normal;
    context.view = view;

    const float ndotView = saturate(dot(normal, view));
    context.ndotView = ndotView;
    const float alpha = roughness * roughness;
    context.distributionAlphaSquared = alpha * alpha;
    const float geometryRoughness = roughness + 1.0f;
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
    float ndoth2 = ndoth * ndoth;

    float denom =
        ndoth2 *
            (context.distributionAlphaSquared - 1.0f) +
        1.0f;
    denom = HIKARI_PI * denom * denom;

    return context.distributionAlphaSquared /
        max(denom, 0.00001f);
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
