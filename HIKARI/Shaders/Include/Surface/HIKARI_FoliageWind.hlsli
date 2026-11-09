#ifndef HIKARI_FOLIAGE_WIND_INCLUDED
#define HIKARI_FOLIAGE_WIND_INCLUDED

static const float2 HIKARI_FOLIAGE_WIND_DIRECTION = normalize(float2(0.82f, 0.57f));
static const float HIKARI_FOLIAGE_WIND_DROP = 0.06f;

float HikariFoliageWindStrength(HikariSurfaceGpuSceneInstance instance)
{
    return clamp(instance.fxUser[0].x, 0.0f, 5.0f);
}

// sway の係数和と tipWeight は共に 1 以下。時刻/UV に依存しない world 包絡。
float3 HikariFoliageWindBoundsExtent(HikariSurfaceGpuSceneInstance instance)
{
    return HikariFoliageWindStrength(instance) *
        float3(HIKARI_FOLIAGE_WIND_DIRECTION.x, HIKARI_FOLIAGE_WIND_DROP,
            HIKARI_FOLIAGE_WIND_DIRECTION.y);
}

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

    const float strength = HikariFoliageWindStrength(instance);
    const float speed = clamp(instance.fxUser[0].y, 0.0f, 20.0f);
    const float spatialFrequency =
        clamp(instance.fxUser[0].z, 0.0f, 20.0f);
    // 実例集の個体は普通の模型で UV が重みではないので、根元からの高さで読む。
    const float tip = (instance.objectIdHigh & 0x80000000u) != 0u
        ? saturate((worldPosition.y - instance.world[1][3]) / max(instance.fxUser[0].w, 0.01f))
        : saturate(uv.y);
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
    worldPosition.xz +=
        HIKARI_FOLIAGE_WIND_DIRECTION * (sway * strength * tipWeight);
    worldPosition.y -=
        abs(sway) * strength * tipWeight * HIKARI_FOLIAGE_WIND_DROP;
    return worldPosition;
}

#endif
