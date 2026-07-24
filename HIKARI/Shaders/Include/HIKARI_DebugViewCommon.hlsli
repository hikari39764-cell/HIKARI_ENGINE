#ifndef HIKARI_DEBUG_VIEW_COMMON_HLSLI
#define HIKARI_DEBUG_VIEW_COMMON_HLSLI

static const uint HIKARI_DEBUG_VIEW_MESHLET_ID = 32u;
static const uint HIKARI_DEBUG_VIEW_CLUSTER_ID = 33u;
static const uint HIKARI_DEBUG_VIEW_SURFACE_ID = 34u;
static const uint HIKARI_DEBUG_VIEW_LOD_LEVEL = 35u;
static const uint HIKARI_DEBUG_VIEW_LOD_HEAT = 36u;
static const uint HIKARI_DEBUG_VIEW_DRAW_BUCKET = 37u;

uint HikariHashDebugId(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float3 HikariDebugColorFromId(uint value)
{
    uint h = HikariHashDebugId(value + 1u);
    return float3(
        0.18f + float((h >> 0u) & 255u) / 255.0f * 0.78f,
        0.18f + float((h >> 8u) & 255u) / 255.0f * 0.78f,
        0.18f + float((h >> 16u) & 255u) / 255.0f * 0.78f);
}

float3 HikariLodDebugColor(uint lodIndex)
{
    if (lodIndex == 0u) return float3(0.95f, 0.20f, 0.18f);
    if (lodIndex == 1u) return float3(0.95f, 0.70f, 0.16f);
    if (lodIndex == 2u) return float3(0.38f, 0.86f, 0.30f);
    if (lodIndex == 3u) return float3(0.18f, 0.72f, 0.96f);
    return float3(0.55f, 0.38f, 0.95f);
}

float3 HikariLodHeatColor(uint lodIndex)
{
    float t = saturate(float(lodIndex) / 4.0f);
    return lerp(float3(1.0f, 0.16f, 0.10f), float3(0.14f, 0.45f, 1.0f), t);
}

bool HikariTryResolveGeometryDebugView(
    uint debugView,
    uint debugClusterId,
    uint debugSurfaceId,
    uint debugLodIndex,
    uint debugDrawBucket,
    float alpha,
    out float4 debugColor)
{
    if (debugView == HIKARI_DEBUG_VIEW_MESHLET_ID)
    {
        uint meshletKey =
            debugClusterId ^
            (debugSurfaceId * 1664525u) ^
            (debugLodIndex * 1013904223u);
        debugColor = float4(HikariDebugColorFromId(meshletKey), alpha);
        return true;
    }

    if (debugView == HIKARI_DEBUG_VIEW_CLUSTER_ID)
    {
        debugColor = float4(HikariDebugColorFromId(debugClusterId), alpha);
        return true;
    }

    if (debugView == HIKARI_DEBUG_VIEW_SURFACE_ID)
    {
        debugColor = float4(HikariDebugColorFromId(debugSurfaceId), alpha);
        return true;
    }

    if (debugView == HIKARI_DEBUG_VIEW_LOD_LEVEL)
    {
        debugColor = float4(HikariLodDebugColor(debugLodIndex), alpha);
        return true;
    }

    if (debugView == HIKARI_DEBUG_VIEW_LOD_HEAT)
    {
        debugColor = float4(HikariLodHeatColor(debugLodIndex), alpha);
        return true;
    }

    if (debugView == HIKARI_DEBUG_VIEW_DRAW_BUCKET)
    {
        float3 bucketColor = debugDrawBucket == 0u
            ? float3(0.28f, 0.92f, 0.48f)
            : float3(0.25f, 0.68f, 1.0f);
        debugColor = float4(bucketColor, alpha);
        return true;
    }

    debugColor = float4(0.0f, 0.0f, 0.0f, alpha);
    return false;
}

#endif
