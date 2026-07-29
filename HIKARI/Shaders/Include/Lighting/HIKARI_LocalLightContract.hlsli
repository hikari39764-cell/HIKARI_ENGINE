#ifndef HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED
#define HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED

static const uint HIKARI_LOCAL_LIGHT_FRAME_CAPACITY = 8u;
static const uint HIKARI_LOCAL_LIGHT_TYPE_POINT = 0u;
static const uint HIKARI_LOCAL_LIGHT_TYPE_SPOT = 1u;

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
    float range)
{
    float attenuation =
        saturate(1.0f - distanceToLight / max(range, 0.001f));
    return attenuation * attenuation;
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
    if (lightType != HIKARI_LOCAL_LIGHT_TYPE_POINT)
    {
        return false;
    }

    const float3 toLight =
        light.positionRange.xyz - worldPosition;
    const float distanceToLight = length(toLight);
    const float attenuation = HikariLocalLightRangeAttenuation(
        distanceToLight,
        light.positionRange.w);

    sample.directionToLight =
        distanceToLight > 1e-5f
        ? toLight / distanceToLight
        : float3(0.0f, 1.0f, 0.0f);
    sample.radiance =
        light.colorIntensity.rgb *
        light.colorIntensity.a *
        attenuation;
    sample.distanceToLight = distanceToLight;
    sample.attenuation = attenuation;
    return attenuation > 0.0f;
}

#endif
