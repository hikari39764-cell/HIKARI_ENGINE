#ifndef HIKARI_GPU_DRIVEN_WATER_DEFORM_INCLUDED
#define HIKARI_GPU_DRIVEN_WATER_DEFORM_INCLUDED

#ifndef HIKARI_GPU_DRIVEN_WATER_ITERATIONS
#define HIKARI_GPU_DRIVEN_WATER_ITERATIONS 30
#endif

#ifndef HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM
#define HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM 1
#endif

#if HIKARI_GPU_DRIVEN_ENABLE_WATER_DEFORM

float2 HikariWaterWaveDx(
    float2 position,
    float2 direction,
    float frequency,
    float timeShift)
{
    direction = normalize(direction);

    float phase = dot(direction, position) * frequency + timeShift;
    float wave = exp(cos(phase) - 1.0f);
    float dx = wave * cos(phase);

    return float2(wave, -dx);
}

float HikariGetWaterWaves(float2 position, float time, float drag)
{
    float phaseOffset = length(position) * 0.1f;
    float directionSeed = 0.0f;
    float frequency = 1.0f;
    float timeMultiplier = 2.0f;
    float weight = 1.0f;

    float valueSum = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (int i = 0; i < HIKARI_GPU_DRIVEN_WATER_ITERATIONS; ++i)
    {
        float2 dir = float2(sin(directionSeed), cos(directionSeed));
        float2 waveData =
            HikariWaterWaveDx(
                position,
                dir,
                frequency,
                time * timeMultiplier + phaseOffset);

        position += dir * waveData.y * weight * drag;

        valueSum += waveData.x * weight;
        weightSum += weight;

        weight = lerp(weight, 0.0f, 0.2f);
        frequency *= 1.18f;
        timeMultiplier *= 1.07f;
        directionSeed += 1232.399963f;
    }

    return valueSum / max(weightSum, 0.0001f);
}

float HikariWaterHeight(
    float2 localXZ,
    float time,
    float waveSpeed,
    float waveHeight,
    float waveScale,
    float drag)
{
    float2 p = localXZ * waveScale;
    float h = HikariGetWaterWaves(p, time * waveSpeed, drag);
    return (h - 0.5f) * waveHeight;
}

float3 HikariWaterNormalLocal(
    float2 localXZ,
    float time,
    float waveSpeed,
    float waveHeight,
    float waveScale,
    float drag)
{
    const float e = 0.08f;

    float h = HikariWaterHeight(localXZ, time, waveSpeed, waveHeight, waveScale, drag);
    float hx = HikariWaterHeight(localXZ + float2(e, 0.0f), time, waveSpeed, waveHeight, waveScale, drag);
    float hz = HikariWaterHeight(localXZ + float2(0.0f, e), time, waveSpeed, waveHeight, waveScale, drag);

    return normalize(float3(h - hx, e, h - hz));
}

void HikariApplyGpuDrivenWaterDeform(
    HikariSurfaceGpuSceneInstance instance,
    float time,
    inout float3 localPosition,
    inout float3 localNormal)
{
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX) == 0u)
    {
        return;
    }

    float waveSpeed = instance.fxUser[0].x;
    float waveHeight = instance.fxUser[0].y;
    float waveScale = max(instance.fxUser[0].z, 0.001f);
    float drag = instance.fxUser[1].w;
    if (drag <= 0.0001f)
    {
        drag = 0.38f;
    }

    const float2 localXZ = localPosition.xz;
    localPosition.y +=
        HikariWaterHeight(localXZ, time, waveSpeed, waveHeight, waveScale, drag);
    localNormal =
        HikariWaterNormalLocal(localXZ, time, waveSpeed, waveHeight, waveScale, drag);
}

#else

void HikariApplyGpuDrivenWaterDeform(
    HikariSurfaceGpuSceneInstance instance,
    float time,
    inout float3 localPosition,
    inout float3 localNormal)
{
}

#endif

#endif
