#ifndef HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED
#define HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED

static const uint HIKARI_LOCAL_LIGHT_TYPE_POINT = 0u;
static const uint HIKARI_LOCAL_LIGHT_TYPE_SPOT = 1u;
static const uint HIKARI_LOCAL_LIGHT_FALLOFF_INVERSE_SQUARE = 1u;

// Keep this layout synchronized with
// RENDER3D::LIGHTING::LocalLightGpuData.
struct HikariLocalLightGpuData
{
    float4 positionRange;
    float4 colorIntensity;
    float4 directionInnerCos;
    float4 outerCosFalloffTypeFlags;
};

struct HikariLocalLightSample
{
    float3 directionToLight;
    float3 radiance;
    float distanceToLight;
    float attenuation;
};

float HikariLocalLightRangeAttenuation(
    float distanceToLight,
    float range,
    uint distanceFalloff)
{
    if (range <= 0.0f || distanceToLight >= range)
        return 0.0f;
    // 距離の単位はワールド単位。近距離 0.01 以下の特異点だけを制限する。
    // 範囲端を滑らかに切るため、範囲を変えても近傍の照度はほぼ一定。
    if (distanceFalloff == HIKARI_LOCAL_LIGHT_FALLOFF_INVERSE_SQUARE)
    {
        const float normalizedDistance = distanceToLight / range;
        const float squaredDistance = normalizedDistance * normalizedDistance;
        const float window = saturate(1.0f - squaredDistance * squaredDistance);
        return window * window / max(distanceToLight * distanceToLight, 1e-4f);
    }
    float attenuation =
        saturate(1.0f - distanceToLight / max(range, 0.001f));
    return attenuation * attenuation;
}

float HikariLocalLightConeAttenuation(
    float coneCos, float innerCos, float outerCos, float angularFalloff)
{
    const float weight = saturate((coneCos - outerCos) /
        max(innerCos - outerCos, 1e-4f));
    return pow(weight, max(angularFalloff, 0.1f));
}

bool HikariResolveLocalLightSample(
    HikariLocalLightGpuData light,
    float3 worldPosition,
    out HikariLocalLightSample sample)
{
    sample.directionToLight = float3(0.0f, 1.0f, 0.0f);
    sample.radiance = 0.0f.xxx;
    sample.distanceToLight = 0.0f;
    sample.attenuation = 0.0f;

    const uint lightType =
        (uint)round(light.outerCosFalloffTypeFlags.z);
    if (lightType != HIKARI_LOCAL_LIGHT_TYPE_POINT &&
        lightType != HIKARI_LOCAL_LIGHT_TYPE_SPOT)
    {
        return false;
    }

    const float3 toLight =
        light.positionRange.xyz - worldPosition;
    const float distanceToLight = length(toLight);
    float attenuation = HikariLocalLightRangeAttenuation(
        distanceToLight,
        light.positionRange.w,
        (uint)round(light.outerCosFalloffTypeFlags.w));

    sample.directionToLight =
        distanceToLight > 1e-5f
        ? toLight / distanceToLight
        : float3(0.0f, 1.0f, 0.0f);

    if (lightType == HIKARI_LOCAL_LIGHT_TYPE_SPOT)
    {
        const float3 spotDirection =
            light.directionInnerCos.xyz;
        const float coneCos = dot(
            spotDirection,
            -sample.directionToLight);
        const float innerCos = light.directionInnerCos.w;
        const float outerCos =
            light.outerCosFalloffTypeFlags.x;
        attenuation *= HikariLocalLightConeAttenuation(
            coneCos, innerCos, outerCos, light.outerCosFalloffTypeFlags.y);
    }

    sample.radiance =
        light.colorIntensity.rgb *
        light.colorIntensity.a *
        attenuation;
    sample.distanceToLight = distanceToLight;
    sample.attenuation = attenuation;
    return attenuation > 0.0f;
}

#endif
