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

float HikariDistributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = saturate(dot(n, h));
    float ndoth2 = ndoth * ndoth;

    float denom = ndoth2 * (a2 - 1.0f) + 1.0f;
    denom = HIKARI_PI * denom * denom;

    return a2 / max(denom, 0.00001f);
}

float HikariGeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    return ndotv / max(ndotv * (1.0f - k) + k, 0.00001f);
}

float HikariGeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    float ndotv = saturate(dot(n, v));
    float ndotl = saturate(dot(n, l));
    float ggxV = HikariGeometrySchlickGGX(ndotv, roughness);
    float ggxL = HikariGeometrySchlickGGX(ndotl, roughness);
    return ggxV * ggxL;
}

float3 HikariEvaluateDirectPbr(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float3 n,
    float3 v,
    float3 l,
    float3 lightColor,
    float lightIntensity)
{
    float3 h = normalize(v + l);
    float ndotv = max(saturate(dot(n, v)), 0.0001f);
    float ndotl = saturate(dot(n, l));

    if (ndotl <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 F0 = HikariSpecularF0(baseColor, metallic, specularColor, specularFactor);
    float3 F = HikariFresnelSchlick(saturate(dot(h, v)), F0);
    float D = HikariDistributionGGX(n, h, roughness);
    float G = HikariGeometrySmith(n, v, l, roughness);

    float3 numerator = D * G * F;
    float denominator = max(4.0f * ndotv * ndotl, 0.0001f);
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);
    float3 diffuse = kD * baseColor / HIKARI_PI;

    return (diffuse + specular) * lightColor * lightIntensity * ndotl;
}

#endif
