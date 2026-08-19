#ifndef HIKARI_FOLIAGE_WIND_INCLUDED
#define HIKARI_FOLIAGE_WIND_INCLUDED

// Foliage geometry stores root-to-tip weight in uv.y. The deformation is
// evaluated in world space so terrain/object yaw does not rotate the authored
// wind direction. Forward, depth, aux, and meshlet shadow paths call this
// single function to keep visible and depth geometry coherent.
float4 HikariApplyFoliageWindWorldPosition(
    HikariSurfaceGpuSceneInstance instance,
    float timeSeconds,
    float2 uv,
    float4 worldPosition)
{
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND) == 0u)
    {
        return worldPosition;
    }

    const float strength = clamp(instance.fxUser[0].x, 0.0f, 5.0f);
    const float speed = clamp(instance.fxUser[0].y, 0.0f, 20.0f);
    const float spatialFrequency =
        clamp(instance.fxUser[0].z, 0.0f, 20.0f);
    const float tip = saturate(uv.y);
    const float tipWeight = tip * tip * (3.0f - 2.0f * tip);
    const float objectPhase =
        (float)(instance.objectIdLow & 1023u) *
        (6.28318530718f / 1024.0f);
    const float phase =
        dot(worldPosition.xz, float2(0.79f, 0.61f)) * spatialFrequency +
        timeSeconds * speed + objectPhase;
    const float sway =
        sin(phase) * 0.72f +
        sin(phase * 1.73f + 1.21f) * 0.28f;
    const float2 windDirection = normalize(float2(0.82f, 0.57f));
    worldPosition.xz +=
        windDirection * (sway * strength * tipWeight);
    worldPosition.y -=
        abs(sway) * strength * tipWeight * 0.06f;
    return worldPosition;
}

#endif
