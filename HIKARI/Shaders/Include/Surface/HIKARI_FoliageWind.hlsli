#ifndef HIKARI_FOLIAGE_WIND_INCLUDED
#define HIKARI_FOLIAGE_WIND_INCLUDED

static const float2 HIKARI_FOLIAGE_WIND_DIRECTION = normalize(float2(0.82f, 0.57f));
static const float HIKARI_FOLIAGE_WIND_DROP = 0.06f;
// 実例集の三層の風の配分（水平の合計は 1 以下。上下は枝の弾みと葉の分）。
static const float HIKARI_FOLIAGE_WIND_TRUNK = 0.62f;
static const float HIKARI_FOLIAGE_WIND_BRANCH = 0.3f;
static const float HIKARI_FOLIAGE_WIND_LEAF = 0.05f;

float HikariFoliageWindStrength(HikariSurfaceGpuSceneInstance instance)
{
    return clamp(instance.fxUser[0].x, 0.0f, 5.0f);
}

// sway の係数和と tipWeight は共に 1 以下。時刻/UV に依存しない world 包絡。
float3 HikariFoliageWindBoundsExtent(HikariSurfaceGpuSceneInstance instance)
{
    if ((instance.objectIdHigh & 0x80000000u) != 0u)
    {
        return HikariFoliageWindStrength(instance) * float3(1.0f, 0.2f, 1.0f);
    }
    return HikariFoliageWindStrength(instance) *
        float3(HIKARI_FOLIAGE_WIND_DIRECTION.x, HIKARI_FOLIAGE_WIND_DROP,
            HIKARI_FOLIAGE_WIND_DIRECTION.y);
}

// 実例集の個体は普通の模型で、風の重みの頂点色が無い。根元からの高さと幹からの距離で三層を重ねる（Crysis の主曲げと
// 細部曲げの考え方）。幹：高さで曲がり、世界座標を流れる風の塊で森全体がうねる。枝：幹から遠いほど大きく、上下にも弾む。
// 葉：高い周波数で小さく震える。時刻と位置だけで決まるので、深度と色の pass で同じ位置になる。
float4 HikariApplyInstanceWindWorldPosition(
    HikariSurfaceGpuSceneInstance instance,
    float timeSeconds,
    float4 worldPosition)
{
    const float strength = HikariFoliageWindStrength(instance);
    const float speed = clamp(instance.fxUser[0].y, 0.0f, 20.0f);
    const float height = max(instance.fxUser[0].w, 0.01f);
    const float3 root = float3(instance.world[0][3], instance.world[1][3], instance.world[2][3]);
    const float3 offset = worldPosition.xyz - root;
    const float tip = saturate(offset.y / height);
    const float reach = saturate(length(offset.xz) / (height * 0.5f));
    const float objectPhase = (float)(instance.objectIdLow & 1023u) * (6.28318530718f / 1024.0f);
    const float across = dot(root.xz, HIKARI_FOLIAGE_WIND_DIRECTION);
    const float gust = 0.6f + 0.4f * sin(across * 0.021f - timeSeconds * speed * 0.27f);
    const float trunk =
        (0.55f + 0.45f * sin(across * 0.06f - timeSeconds * speed * 0.8f + objectPhase * 0.35f)) *
        tip * tip * (3.0f - 2.0f * tip);
    const float branchPhase = timeSeconds * speed * 1.9f + objectPhase * 3.0f + dot(offset.xz, float2(1.3f, 1.7f));
    const float branch = sin(branchPhase) * reach * reach;
    const float leafPhase = timeSeconds * speed * 5.3f + dot(worldPosition.xyz, float3(5.1f, 3.3f, 4.3f));
    const float leaf = sin(leafPhase) * sin(leafPhase * 0.37f + 1.7f) * reach;
    worldPosition.xz += HIKARI_FOLIAGE_WIND_DIRECTION * strength * gust *
        (trunk * HIKARI_FOLIAGE_WIND_TRUNK + branch * HIKARI_FOLIAGE_WIND_BRANCH);
    worldPosition.xz += float2(0.71f, -0.71f) * strength * leaf * HIKARI_FOLIAGE_WIND_LEAF;
    worldPosition.y += strength * gust * (branch * HIKARI_FOLIAGE_WIND_BRANCH * 0.45f + leaf * HIKARI_FOLIAGE_WIND_LEAF);
    return worldPosition;
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

    if ((instance.objectIdHigh & 0x80000000u) != 0u)
    {
        return HikariApplyInstanceWindWorldPosition(instance, timeSeconds, worldPosition);
    }

    const float strength = HikariFoliageWindStrength(instance);
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
    worldPosition.xz +=
        HIKARI_FOLIAGE_WIND_DIRECTION * (sway * strength * tipWeight);
    worldPosition.y -=
        abs(sway) * strength * tipWeight * HIKARI_FOLIAGE_WIND_DROP;
    return worldPosition;
}

#endif
