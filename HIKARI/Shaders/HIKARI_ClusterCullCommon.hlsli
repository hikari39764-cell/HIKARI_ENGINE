#ifndef HIKARI_CLUSTER_CULL_COMMON_INCLUDED
#define HIKARI_CLUSTER_CULL_COMMON_INCLUDED

#include "Include/Cluster/HIKARI_ClusterCullContract.hlsli"

float4 HikariClusterCullMatrixRow0(float4x4 matrix)
{
    return float4(
        matrix._11,
        matrix._12,
        matrix._13,
        matrix._14);
}

float4 HikariClusterCullMatrixRow1(float4x4 matrix)
{
    return float4(
        matrix._21,
        matrix._22,
        matrix._23,
        matrix._24);
}

float4 HikariClusterCullMatrixRow2(float4x4 matrix)
{
    return float4(
        matrix._31,
        matrix._32,
        matrix._33,
        matrix._34);
}

float4 HikariClusterCullMatrixRow3(float4x4 matrix)
{
    return float4(
        matrix._41,
        matrix._42,
        matrix._43,
        matrix._44);
}

float4 HikariClusterCullViewProjRow0()
{
    return HikariClusterCullMatrixRow0(gClusterCullViewProj);
}

float4 HikariClusterCullViewProjRow1()
{
    return HikariClusterCullMatrixRow1(gClusterCullViewProj);
}

float4 HikariClusterCullViewProjRow2()
{
    return HikariClusterCullMatrixRow2(gClusterCullViewProj);
}

float4 HikariClusterCullViewProjRow3()
{
    return HikariClusterCullMatrixRow3(gClusterCullViewProj);
}

bool HikariClusterCullPlaneVisible(float4 plane, float3 center, float radius)
{
    float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }

    float distance = dot(plane.xyz, center) + plane.w;
    return distance >= -radius * planeLength;
}

bool HikariClusterCullSphereVisible(float4 boundsCenterRadius)
{
    if (gClusterCullEnableFrustumCull == 0)
    {
        return true;
    }

    float3 center = boundsCenterRadius.xyz;
    float radius = max(boundsCenterRadius.w, 0.0f);

    // Surface bounds are still the draw seed here, so use clip-space plane tests
    // instead of an NDC radius approximation. The latter over-culls at the
    // screen edge and can drop a whole surface while part of it is still visible.
    float4 row0 = HikariClusterCullViewProjRow0();
    float4 row1 = HikariClusterCullViewProjRow1();
    float4 row2 = HikariClusterCullViewProjRow2();
    float4 row3 = HikariClusterCullViewProjRow3();

    return
        HikariClusterCullPlaneVisible(row3 + row0, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row0, center, radius) &&
        HikariClusterCullPlaneVisible(row3 + row1, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row1, center, radius) &&
        HikariClusterCullPlaneVisible(row2, center, radius) &&
        HikariClusterCullPlaneVisible(row3 - row2, center, radius);
}

float HikariClusterCullProjectedWorldLengthWithMatrix(
    float4x4 viewProj,
    float3 worldCenter,
    float worldLength)
{
    float4 clipCenter =
        mul(viewProj, float4(worldCenter, 1.0f));
    float projectionScale =
        max(
            length(HikariClusterCullMatrixRow0(viewProj).xyz),
            length(HikariClusterCullMatrixRow1(viewProj).xyz));
    return max(worldLength, 0.0f) *
        projectionScale /
        max(abs(clipCenter.w), 0.0001f);
}

float HikariClusterCullProjectedWorldLength(float3 worldCenter, float worldLength)
{
    return HikariClusterCullProjectedWorldLengthWithMatrix(
        gClusterCullViewProj,
        worldCenter,
        worldLength);
}

float HikariClusterCullProjectedScreenRadius(float4 boundsCenterRadius)
{
    return HikariClusterCullProjectedWorldLength(
        boundsCenterRadius.xyz,
        boundsCenterRadius.w);
}

void HikariClusterCullAddDebugCounter(uint byteOffset, uint value)
{
    if (gClusterCullEnableDebugCounters != 0u)
    {
        gClusterCullCounters.InterlockedAdd(byteOffset, value);
    }
}

uint HikariClusterCullHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

uint HikariClusterCullBuildOcclusionKey(
    uint gpuSceneInstanceIndex,
    uint passKind,
    uint lodIndex,
    uint sectionIndex,
    uint pageIndex,
    uint clusterIndex,
    uint level)
{
    uint key = gpuSceneInstanceIndex + 1u;
    key = HikariClusterCullHash(key ^ ((passKind + 3u) * 0x9e3779b9u));
    key = HikariClusterCullHash(key ^ ((lodIndex + 5u) * 0x85ebca6bu));
    key = HikariClusterCullHash(key ^ ((sectionIndex + 7u) * 0xc2b2ae35u));
    key = HikariClusterCullHash(key ^ ((pageIndex + 11u) * 0x27d4eb2fu));
    key = HikariClusterCullHash(key ^ ((clusterIndex + 13u) * 0x165667b1u));
    key = HikariClusterCullHash(key ^ ((level + 17u) * 0xd3a2646cu));
    return key != 0u ? key : 1u;
}

uint HikariClusterCullHistoryEntryOffset(uint key)
{
    uint capacity = max(gClusterCullOcclusionHistoryCapacity, 1u);
    uint slot = HikariClusterCullHash(key) % capacity;
    return slot * 8u;
}

uint HikariClusterCullHistoryPackState(uint frame, uint visibleMisses, uint streak)
{
    return ((frame & 0xffffu) << 16u) |
        ((visibleMisses & 0xffu) << 8u) |
        (streak & 0xffu);
}

uint HikariClusterCullHistoryFrame(uint state)
{
    return (state >> 16u) & 0xffffu;
}

uint HikariClusterCullHistoryVisibleMisses(uint state)
{
    return (state >> 8u) & 0xffu;
}

uint HikariClusterCullHistoryStreak(uint state)
{
    return state & 0xffu;
}

void HikariClusterCullHistoryResetVisible(uint key)
{
    if (gClusterCullOcclusionHistoryCapacity == 0u || key == 0u)
    {
        return;
    }

    uint offset = HikariClusterCullHistoryEntryOffset(key);
    uint storedKey = gClusterCullOcclusionHistory.Load(offset);
    if (storedKey != key)
    {
        return;
    }
    uint frame = gClusterCullTemporalFrameIndex & 0xffffu;
    uint state = gClusterCullOcclusionHistory.Load(offset + 4u);
    uint lastFrame = HikariClusterCullHistoryFrame(state);
    uint visibleMisses = HikariClusterCullHistoryVisibleMisses(state);
    uint streak = HikariClusterCullHistoryStreak(state);
    uint nextVisibleMisses = lastFrame == frame
        ? visibleMisses
        : min(visibleMisses + 1u, 255u);
    uint nextStreak = nextVisibleMisses >= HIKARI_CLUSTER_CULL_TEMPORAL_VISIBLE_RESET_FRAMES
        ? 0u
        : streak;
    gClusterCullOcclusionHistory.Store(
        offset + 4u,
        HikariClusterCullHistoryPackState(frame, nextVisibleMisses, nextStreak));
    HikariClusterCullAddDebugCounter(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_RESET_COUNT,
        1u);
}

bool HikariClusterCullTemporalOcclusionConfirmed(uint key)
{
    uint confirmFrames = max(gClusterCullHzbOcclusionConfirmFrames, 1u);
    if (confirmFrames <= 1u)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_CONFIRMED_COUNT,
            1u);
        return true;
    }
    if (gClusterCullOcclusionHistoryCapacity == 0u || key == 0u)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_PENDING_COUNT,
            1u);
        return false;
    }

    uint offset = HikariClusterCullHistoryEntryOffset(key);
    uint storedKey = gClusterCullOcclusionHistory.Load(offset);
    if (storedKey != key)
    {
        gClusterCullOcclusionHistory.Store(offset, key);
        gClusterCullOcclusionHistory.Store(
            offset + 4u,
            HikariClusterCullHistoryPackState(
                gClusterCullTemporalFrameIndex,
                0u,
                1u));
        if (storedKey != 0u)
        {
            HikariClusterCullAddDebugCounter(
                HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_COLLISION_COUNT,
                1u);
        }
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_PENDING_COUNT,
            1u);
        return false;
    }

    uint state = gClusterCullOcclusionHistory.Load(offset + 4u);
    uint currentFrame = gClusterCullTemporalFrameIndex & 0xffffu;
    uint lastFrame = HikariClusterCullHistoryFrame(state);
    uint visibleMisses = HikariClusterCullHistoryVisibleMisses(state);
    uint streak = HikariClusterCullHistoryStreak(state);
    uint frameDelta = (currentFrame - lastFrame) & 0xffffu;
    uint nextStreak = 1u;
    if (lastFrame == currentFrame)
    {
        nextStreak = max(streak, 1u);
    }
    else if (frameDelta <= HIKARI_CLUSTER_CULL_TEMPORAL_CONFIRM_MAX_FRAME_GAP &&
        visibleMisses < HIKARI_CLUSTER_CULL_TEMPORAL_VISIBLE_RESET_FRAMES)
    {
        nextStreak = min(streak + 1u, 255u);
    }

    gClusterCullOcclusionHistory.Store(
        offset + 4u,
        HikariClusterCullHistoryPackState(currentFrame, 0u, nextStreak));

    if (nextStreak >= confirmFrames)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_CONFIRMED_COUNT,
            1u);
        return true;
    }

    HikariClusterCullAddDebugCounter(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_PENDING_COUNT,
        1u);
    return false;
}

bool HikariClusterCullHzbOcclusionAllowed(uint passKind)
{
    return
        gClusterCullEnableHzbOcclusion != 0u &&
        passKind != HIKARI_CLUSTER_CULL_PASS_SHADOW &&
        passKind != HIKARI_CLUSTER_CULL_PASS_DEPTH_PREPASS &&
        gClusterCullHzbWidth > 0u &&
        gClusterCullHzbHeight > 0u;
}

struct HikariClusterCullHzbQuery
{
    float2 minUv;
    float2 maxUv;
    float nearestDepth;
    float maxExtentPixels;
    uint flags;
};

uint HikariClusterCullMipDim(uint baseDim, uint mipLevel)
{
    return max(1u, baseDim >> min(mipLevel, 31u));
}

uint HikariClusterCullSelectHzbMip(float maxExtentPixels)
{
    if (gClusterCullHzbMipCount <= 1u)
    {
        return 0u;
    }

    // Pick a mip where the projected bounds cover a few texels. Sampling a
    // projected rectangle is more stable than guessing from a sphere radius.
    float desiredMip = floor(log2(max(maxExtentPixels * 0.5f, 1.0f)));
    return min((uint)max(desiredMip, 0.0f), gClusterCullHzbMipCount - 1u);
}

float HikariClusterCullLoadHzbDepth(int2 pixel, uint mipLevel)
{
    int2 maxPixel = int2(
        max((int)HikariClusterCullMipDim(gClusterCullHzbWidth, mipLevel) - 1, 0),
        max((int)HikariClusterCullMipDim(gClusterCullHzbHeight, mipLevel) - 1, 0));
    uint2 clampedPixel = (uint2)clamp(pixel, int2(0, 0), maxPixel);
    return gClusterCullHzb.Load(int3(clampedPixel, mipLevel));
}

float HikariClusterCullLoadHzbMaxDepthInRect(
    float2 minUv,
    float2 maxUv,
    uint mipLevel)
{
    uint mipWidth = HikariClusterCullMipDim(gClusterCullHzbWidth, mipLevel);
    uint mipHeight = HikariClusterCullMipDim(gClusterCullHzbHeight, mipLevel);
    float2 mipSize = float2((float)mipWidth, (float)mipHeight);
    int2 minPixel = int2(floor(saturate(minUv) * mipSize));
    int2 maxPixel = int2(ceil(saturate(maxUv) * mipSize - 1.0f));
    maxPixel = max(maxPixel, minPixel);

    float maxHzbDepth = 0.0f;
    [unroll]
    for (uint y = 0u; y < 3u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 3u; ++x)
        {
            float2 t = float2((float)x, (float)y) * 0.5f;
            int2 pixel = int2(round(lerp((float2)minPixel, (float2)maxPixel, t)));
            maxHzbDepth = max(maxHzbDepth, HikariClusterCullLoadHzbDepth(pixel, mipLevel));
        }
    }
    return maxHzbDepth;
}

float3 HikariClusterCullBoundsCorner(float4 boundsMin, float4 boundsMax, uint cornerIndex)
{
    return float3(
        (cornerIndex & 1u) != 0u ? boundsMax.x : boundsMin.x,
        (cornerIndex & 2u) != 0u ? boundsMax.y : boundsMin.y,
        (cornerIndex & 4u) != 0u ? boundsMax.z : boundsMin.z);
}

uint HikariClusterCullBuildHzbQuery(
    float4x4 world,
    float4 boundsMin,
    float4 boundsMax,
    out HikariClusterCullHzbQuery query)
{
    query.minUv = float2(1.0f, 1.0f);
    query.maxUv = float2(0.0f, 0.0f);
    query.nearestDepth = 1.0f;
    query.maxExtentPixels = 0.0f;
    query.flags = 0u;

    uint projectedCornerCount = 0u;
    [unroll]
    for (uint cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
    {
        float3 localCorner =
            HikariClusterCullBoundsCorner(boundsMin, boundsMax, cornerIndex);
        float3 worldCorner = mul(world, float4(localCorner, 1.0f)).xyz;
        float4 clip = mul(gClusterCullHzbViewProj, float4(worldCorner, 1.0f));
        if (clip.w <= 0.0001f)
        {
            return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
        }

        float3 ndc = clip.xyz / clip.w;
        if (ndc.z <= 0.0f)
        {
            return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
        }

        float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        query.minUv = min(query.minUv, uv);
        query.maxUv = max(query.maxUv, uv);
        query.nearestDepth = min(query.nearestDepth, min(ndc.z, 1.0f));
        ++projectedCornerCount;
    }
    if (projectedCornerCount == 0u)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }

    if (query.maxUv.x <= 0.0f ||
        query.minUv.x >= 1.0f ||
        query.maxUv.y <= 0.0f ||
        query.minUv.y >= 1.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN;
    }

    query.minUv = saturate(query.minUv);
    query.maxUv = saturate(query.maxUv);
    float2 extentPixels =
        max((query.maxUv - query.minUv) *
            float2((float)gClusterCullHzbWidth, (float)gClusterCullHzbHeight),
            float2(1.0f, 1.0f));
    query.maxExtentPixels = max(extentPixels.x, extentPixels.y);
    if (query.maxExtentPixels <= 0.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }
    if (query.maxExtentPixels > max(gClusterCullHzbMaxScreenRadiusPixels, 1.0f))
    {
        query.flags |= HIKARI_CLUSTER_CULL_HZB_QUERY_FLAG_LARGE_RECT;
    }

    return query.nearestDepth > 0.0f && query.nearestDepth < 1.0f
        ? HIKARI_CLUSTER_CULL_HZB_QUERY_OK
        : HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
}

uint HikariClusterCullBuildHzbSphereQuery(
    float4x4 world,
    float4 boundsMin,
    float4 boundsMax,
    out HikariClusterCullHzbQuery query)
{
    query.minUv = float2(1.0f, 1.0f);
    query.maxUv = float2(0.0f, 0.0f);
    query.nearestDepth = 1.0f;
    query.maxExtentPixels = 0.0f;
    query.flags = 0u;

    float3 localCenter = (boundsMin.xyz + boundsMax.xyz) * 0.5f;
    float3 localExtents = max(boundsMax.xyz - localCenter, float3(0.0f, 0.0f, 0.0f));
    float localRadius = length(localExtents);
    if (localRadius <= 0.000001f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }

    float3 worldCenter = mul(world, float4(localCenter, 1.0f)).xyz;
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    float worldScale = max(length(axisX), max(length(axisY), length(axisZ)));
    float worldRadius = max(localRadius * worldScale, 0.0f);
    if (worldRadius <= 0.000001f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }

    float4 clipCenter = mul(gClusterCullHzbViewProj, float4(worldCenter, 1.0f));
    float4 row2 = HikariClusterCullMatrixRow2(gClusterCullHzbViewProj);
    float4 row3 = HikariClusterCullMatrixRow3(gClusterCullHzbViewProj);
    float wRadius = length(row3.xyz) * worldRadius;
    if (clipCenter.w <= wRadius + 0.0001f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
    }

    float3 ndcCenter = clipCenter.xyz / clipCenter.w;
    if (ndcCenter.z <= 0.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
    }
    if (ndcCenter.z >= 1.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN;
    }

    float screenRadiusNdc =
        HikariClusterCullProjectedWorldLengthWithMatrix(
            gClusterCullHzbViewProj,
            worldCenter,
            worldRadius);
    if (screenRadiusNdc <= 0.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }

    float2 centerUv = float2(
        ndcCenter.x * 0.5f + 0.5f,
        0.5f - ndcCenter.y * 0.5f);
    float2 radiusUv = float2(screenRadiusNdc * 0.5f, screenRadiusNdc * 0.5f);
    query.minUv = centerUv - radiusUv;
    query.maxUv = centerUv + radiusUv;
    if (query.maxUv.x <= 0.0f ||
        query.minUv.x >= 1.0f ||
        query.maxUv.y <= 0.0f ||
        query.minUv.y >= 1.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN;
    }

    query.minUv = saturate(query.minUv);
    query.maxUv = saturate(query.maxUv);
    float2 extentPixels =
        max((query.maxUv - query.minUv) *
            float2((float)gClusterCullHzbWidth, (float)gClusterCullHzbHeight),
            float2(1.0f, 1.0f));
    query.maxExtentPixels = max(extentPixels.x, extentPixels.y);
    if (query.maxExtentPixels <= 0.0f)
    {
        return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
    }
    if (query.maxExtentPixels > max(gClusterCullHzbMaxScreenRadiusPixels, 1.0f))
    {
        query.flags |= HIKARI_CLUSTER_CULL_HZB_QUERY_FLAG_LARGE_RECT;
    }

    float zRadius = length(row2.xyz) * worldRadius;
    float nearestClipZ = clipCenter.z - zRadius;
    float nearestClipW = max(clipCenter.w + wRadius, 0.0001f);
    query.nearestDepth = nearestClipZ / nearestClipW;
    return query.nearestDepth > 0.0f && query.nearestDepth < 1.0f
        ? HIKARI_CLUSTER_CULL_HZB_QUERY_OK
        : HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
}

void HikariClusterCullRecordHzbQueryReject(uint rejectReason)
{
    if (rejectReason == HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_NEAR_PLANE_REJECTED_COUNT,
            1);
    }
    else if (rejectReason == HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_OFFSCREEN_REJECTED_COUNT,
            1);
    }
}

bool HikariClusterCullTryHzbOccluded(
    uint passKind,
    float4x4 world,
    float4 boundsMin,
    float4 boundsMax,
    uint occlusionKey,
    bool allowLargeRectOcclusion,
    out bool tested)
{
    tested = false;
    HikariClusterCullAddDebugCounter(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_TRY_COUNT,
        1);

    if (gClusterCullEnableHzbOcclusion == 0u ||
        gClusterCullHzbWidth == 0u ||
        gClusterCullHzbHeight == 0u ||
        gClusterCullHzbMipCount == 0u)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_INVALID_REJECTED_COUNT,
            1);
        return false;
    }

    if (passKind == HIKARI_CLUSTER_CULL_PASS_SHADOW ||
        passKind == HIKARI_CLUSTER_CULL_PASS_DEPTH_PREPASS)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_PASS_REJECTED_COUNT,
            1);
        return false;
    }

    HikariClusterCullHzbQuery query;
    uint queryRejectReason =
        HikariClusterCullBuildHzbQuery(world, boundsMin, boundsMax, query);
    if (queryRejectReason != HIKARI_CLUSTER_CULL_HZB_QUERY_OK)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_AABB_REJECTED_COUNT,
            1);
        HikariClusterCullRecordHzbQueryReject(queryRejectReason);

        queryRejectReason =
            HikariClusterCullBuildHzbSphereQuery(world, boundsMin, boundsMax, query);
        if (queryRejectReason != HIKARI_CLUSTER_CULL_HZB_QUERY_OK)
        {
            HikariClusterCullAddDebugCounter(
                HIKARI_CLUSTER_CULL_COUNTER_HZB_SPHERE_REJECTED_COUNT,
                1);
            HikariClusterCullRecordHzbQueryReject(queryRejectReason);
            HikariClusterCullHistoryResetVisible(occlusionKey);
            return false;
        }
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_SPHERE_ACCEPTED_COUNT,
            1);
    }
    else
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_AABB_ACCEPTED_COUNT,
            1);
    }

    HikariClusterCullAddDebugCounter(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_QUERY_ACCEPTED_COUNT,
        1);
    
    if ((query.flags & HIKARI_CLUSTER_CULL_HZB_QUERY_FLAG_LARGE_RECT) != 0u)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_LARGE_RECT_COUNT,
            1);
        if (!allowLargeRectOcclusion &&
            gClusterCullHzbAllowLargeRectOcclusion == 0u)
        {
            HikariClusterCullAddDebugCounter(
                HIKARI_CLUSTER_CULL_COUNTER_HZB_LARGE_RECT_SKIPPED_COUNT,
                1);
            HikariClusterCullHistoryResetVisible(occlusionKey);
            return false;
        }
    }

    uint hzbAllowedIndex = 0u;
    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_ALLOWED_COUNT,
        1,
        hzbAllowedIndex);
    if (gClusterCullHzbTestBudget != 0u &&
        hzbAllowedIndex >= gClusterCullHzbTestBudget)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_BUDGET_SKIPPED_COUNT,
            1);
        HikariClusterCullHistoryResetVisible(occlusionKey);
        return false;
    }

    tested = true;
    uint mipLevel = HikariClusterCullSelectHzbMip(query.maxExtentPixels);
    float maxHzbDepth =
        HikariClusterCullLoadHzbMaxDepthInRect(query.minUv, query.maxUv, mipLevel);
    if (!(maxHzbDepth > max(gClusterCullHzbDepthBias, 0.000001f)))
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_HZB_INVALID_REJECTED_COUNT,
            1);
        HikariClusterCullHistoryResetVisible(occlusionKey);
        return false;
    }

    bool rawOccluded =
        query.nearestDepth > maxHzbDepth + max(gClusterCullHzbDepthBias, 0.0f);
    if (!rawOccluded)
    {
        HikariClusterCullHistoryResetVisible(occlusionKey);
        return false;
    }
    HikariClusterCullAddDebugCounter(
        HIKARI_CLUSTER_CULL_COUNTER_HZB_RAW_OCCLUDED_COUNT,
        1);

    return HikariClusterCullTemporalOcclusionConfirmed(occlusionKey);
}

float HikariClusterCullProjectedLodError(
    float4 boundsCenterRadius,
    HikariClusterGeometrySurfaceLodRange range)
{
    return HikariClusterCullProjectedWorldLength(
        boundsCenterRadius.xyz,
        range.geometricError);
}

float4 HikariClusterCullBuildWorldSphere(
    float4x4 world,
    float4 localSphere,
    float4 boundsMin,
    float4 boundsMax)
{
    float3 localCenter = localSphere.xyz;
    float localRadius = localSphere.w;
    if (localRadius <= 0.000001f)
    {
        localCenter = (boundsMin.xyz + boundsMax.xyz) * 0.5f;
        float3 extents = max(boundsMax.xyz - localCenter, float3(0.0f, 0.0f, 0.0f));
        localRadius = length(extents);
    }

    float3 worldCenter = mul(world, float4(localCenter, 1.0f)).xyz;
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    float worldScale = max(length(axisX), max(length(axisY), length(axisZ)));
    return float4(worldCenter, max(localRadius * worldScale, 0.0f));
}

uint HikariClusterCullResolveBucket(uint flags)
{
    return (flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) != 0u
        ? HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED
        : HIKARI_CLUSTER_DRAW_BUCKET_BACK_FACE;
}

bool HikariClusterCullIsDoubleSided(uint flags)
{
    return (flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) != 0u;
}

uint HikariClusterCullPassIndex(uint passKind)
{
    return passKind < HIKARI_CLUSTER_CULL_PASS_COUNT ? passKind : 0u;
}

uint HikariClusterCullPassBucketBaseIndex(uint passKind, uint bucket)
{
    uint passIndex = HikariClusterCullPassIndex(passKind);
    return (passIndex * HIKARI_CLUSTER_DRAW_BUCKET_COUNT + bucket) *
        gClusterCullCandidateCommandBucketCapacity;
}

uint HikariClusterCullPassDrawCounterOffset(uint passKind, uint bucket)
{
    uint passIndex = HikariClusterCullPassIndex(passKind);
    uint bucketOffset = bucket == HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED
        ? HIKARI_CLUSTER_CULL_COUNTER_PASS_DOUBLE_SIDED_DRAW_COUNT
        : HIKARI_CLUSTER_CULL_COUNTER_PASS_BACK_FACE_DRAW_COUNT;
    return HIKARI_CLUSTER_CULL_COUNTER_PASS_BASE +
        passIndex * HIKARI_CLUSTER_CULL_COUNTER_PASS_STRIDE +
        bucketOffset;
}

uint HikariClusterCullPassDrawOverflowCounterOffset(uint passKind, uint bucket)
{
    uint passIndex = HikariClusterCullPassIndex(passKind);
    uint bucketOffset = bucket == HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED
        ? HIKARI_CLUSTER_CULL_COUNTER_PASS_DOUBLE_SIDED_DRAW_OVERFLOW_COUNT
        : HIKARI_CLUSTER_CULL_COUNTER_PASS_BACK_FACE_DRAW_OVERFLOW_COUNT;
    return HIKARI_CLUSTER_CULL_COUNTER_PASS_BASE +
        passIndex * HIKARI_CLUSTER_CULL_COUNTER_PASS_STRIDE +
        bucketOffset;
}

uint HikariClusterCullMergeGapIndexLimit()
{
    return gClusterCullMergeGapIndexLimit != 0u
        ? gClusterCullMergeGapIndexLimit
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_GAP_INDEX_LIMIT;
}

uint HikariClusterCullMergeRunGapBudget()
{
    return gClusterCullMergeRunGapIndexBudget != 0u
        ? gClusterCullMergeRunGapIndexBudget
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_RUN_GAP_BUDGET;
}

uint HikariClusterCullMergeMaxIndexSpan()
{
    return gClusterCullMergeMaxIndexSpan != 0u
        ? gClusterCullMergeMaxIndexSpan
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_MAX_INDEX_SPAN;
}

uint HikariClusterCullMergeClusterGapLimit()
{
    return gClusterCullMergeClusterGapLimit != 0u
        ? gClusterCullMergeClusterGapLimit
        : HIKARI_CLUSTER_CULL_DEFAULT_MERGE_CLUSTER_GAP_LIMIT;
}

uint HikariClusterCullPageTaskGroupSize()
{
    return clamp(gClusterCullPageTaskGroupSize, 1u, 8u);
}

float HikariClusterCullApproxHzbScreenRadiusPixels(float4 worldSphere)
{
    if (gClusterCullHzbWidth == 0u || gClusterCullHzbHeight == 0u)
    {
        return 0.0f;
    }

    float radiusNdc =
        HikariClusterCullProjectedWorldLengthWithMatrix(
            gClusterCullHzbViewProj,
            worldSphere.xyz,
            worldSphere.w);
    return radiusNdc * 0.5f *
        (float)max(gClusterCullHzbWidth, gClusterCullHzbHeight);
}

bool HikariClusterCullShouldTestClusterHzb(float4 worldSphere)
{
    if (gClusterCullClusterHzbMinScreenPixels == 0u)
    {
        return true;
    }

    float screenRadius = HikariClusterCullApproxHzbScreenRadiusPixels(worldSphere);
    if (screenRadius < (float)gClusterCullClusterHzbMinScreenPixels)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_HZB_SMALL_SCREEN_SKIPPED_COUNT,
            1);
        return false;
    }

    if (screenRadius > max(gClusterCullHzbMaxScreenRadiusPixels, 1.0f))
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_HZB_LARGE_SCREEN_SKIPPED_COUNT,
            1);
        return false;
    }

    return true;
}

bool HikariClusterCullShouldTestPageHzb(float4 worldSphere)
{
    if (gClusterCullClusterHzbMinScreenPixels == 0u)
    {
        return true;
    }

    float screenRadius = HikariClusterCullApproxHzbScreenRadiusPixels(worldSphere);
    float minPageRadius = (float)max(gClusterCullClusterHzbMinScreenPixels * 2u, 8u);
    if (screenRadius < minPageRadius)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_HZB_SMALL_SCREEN_SKIPPED_COUNT,
            1);
        return false;
    }

    return true;
}

float3 HikariClusterCullTransformNormalAxis(float4x4 world, float3 localAxis)
{
    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);

    float3 normalAxis =
        localAxis.x * cross(axisY, axisZ) +
        localAxis.y * cross(axisZ, axisX) +
        localAxis.z * cross(axisX, axisY);
    if (length(normalAxis) <= 0.000001f)
    {
        normalAxis = mul(world, float4(localAxis, 0.0f)).xyz;
    }
    if (length(normalAxis) <= 0.000001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return normalize(normalAxis);
}

float HikariClusterCullConeAxisSupportRadius(
    float4x4 world,
    float4 boundsMin,
    float4 boundsMax,
    float3 worldAxis)
{
    float3 localCenter = (boundsMin.xyz + boundsMax.xyz) * 0.5f;
    float3 localExtents = max(boundsMax.xyz - localCenter, float3(0.0f, 0.0f, 0.0f));

    float3 axisX = float3(world._11, world._21, world._31);
    float3 axisY = float3(world._12, world._22, world._32);
    float3 axisZ = float3(world._13, world._23, world._33);
    return max(
        abs(dot(worldAxis, axisX)) * localExtents.x +
        abs(dot(worldAxis, axisY)) * localExtents.y +
        abs(dot(worldAxis, axisZ)) * localExtents.z,
        0.0f);
}

bool HikariClusterCullConeBackfacing(
    float4x4 world,
    HikariMeshCluster cluster,
    float4 worldSphere)
{
    float3 localAxis = cluster.coneAxisCutoff.xyz;
    float localAxisLength = length(localAxis);
    float coneCutoff = cluster.coneAxisCutoff.w;
    if (localAxisLength <= 0.000001f ||
        coneCutoff <= 0.0f ||
        coneCutoff >= 1.0f)
    {
        return false;
    }

    float3 axis = HikariClusterCullTransformNormalAxis(world, localAxis / localAxisLength);
    if (length(axis) <= 0.000001f)
    {
        return false;
    }

    float sphereRadius = max(worldSphere.w, 0.0f);
    if (sphereRadius <= 0.000001f)
    {
        return false;
    }

    float axisRadius = HikariClusterCullConeAxisSupportRadius(
        world,
        cluster.boundsMin,
        cluster.boundsMax,
        axis);
    float coneRadius = min(
        sphereRadius,
        max(axisRadius, sphereRadius * HIKARI_CLUSTER_CULL_CONE_AXIS_RADIUS_FLOOR_SCALE));

    float3 view = worldSphere.xyz - gClusterCullCameraPosition.xyz;
    float viewLength = length(view);
    if (viewLength <= max(0.0001f, coneRadius * HIKARI_CLUSTER_CULL_CONE_NEAR_RADIUS_SCALE))
    {
        return false;
    }

    // meshoptimizer 縺ｮ perspective cone 蛻､螳壹Ｆpsilon 縺ｯ蠅・阜縺ｮ縺｡繧峨▽縺阪ｒ謚代∴繧九◆繧∝ｰ代＠菫晏ｮ育噪縺ｫ縺吶ｋ縲・
    // Sphere formula from meshoptimizer: dot(center - camera, axis) >=
    // cutoff * distance + radius. The extra bias keeps the reject high-confidence.
    float rejectThreshold = coneCutoff * viewLength + coneRadius;
    float stabilityBias = max(
        viewLength * HIKARI_CLUSTER_CULL_CONE_DISTANCE_BIAS,
        coneRadius * HIKARI_CLUSTER_CULL_CONE_RADIUS_BIAS);
    return dot(view, axis) >= rejectThreshold + stabilityBias;
}

uint4 HikariClusterCullEmptyPacket4()
{
    return uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
}

void HikariClusterCullSetPacketIndex(
    inout uint4 packet0,
    inout uint4 packet1,
    inout uint4 packet2,
    inout uint4 packet3,
    uint slot,
    uint clusterIndex)
{
    if (slot < 4u)
    {
        packet0[slot] = clusterIndex;
    }
    else if (slot < 8u)
    {
        packet1[slot - 4u] = clusterIndex;
    }
    else if (slot < 12u)
    {
        packet2[slot - 8u] = clusterIndex;
    }
    else if (slot < 16u)
    {
        packet3[slot - 12u] = clusterIndex;
    }
}

uint HikariClusterCullReserveVisibleClusterListRun()
{
    uint listStart = 0u;
    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_LIST_RESERVED_COUNT,
        HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY,
        listStart);
    if (listStart + HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY >
        gClusterCullVisibleClusterListCapacity)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_LIST_OVERFLOW_COUNT,
            1u);
        return 0xffffffffu;
    }
    return listStart;
}

void HikariClusterCullSetVisibleClusterListIndex(
    uint listStart,
    uint slot,
    uint clusterIndex)
{
    if (listStart == 0xffffffffu ||
        slot >= HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY ||
        listStart + slot >= gClusterCullVisibleClusterListCapacity)
    {
        return;
    }
    gClusterCullVisibleClusterList[listStart + slot] = clusterIndex;
}

uint HikariClusterCullGetPacketIndex(
    uint4 packet0,
    uint4 packet1,
    uint4 packet2,
    uint4 packet3,
    uint slot)
{
    if (slot < 4u)
    {
        return packet0[slot];
    }
    if (slot < 8u)
    {
        return packet1[slot - 4u];
    }
    if (slot < 12u)
    {
        return packet2[slot - 8u];
    }
    if (slot < HIKARI_CLUSTER_CULL_VISIBLE_PACKET_CAPACITY)
    {
        return packet3[slot - 12u];
    }
    return 0xffffffffu;
}

bool HikariClusterCullPromoteVisibleRunToClusterList(
    inout uint runClusterListStart,
    uint runFirstCluster,
    uint runVisibleClusterCount,
    uint runMergedGapCount,
    uint4 packetClusterIndices0,
    uint4 packetClusterIndices1,
    uint4 packetClusterIndices2,
    uint4 packetClusterIndices3)
{
    if (runClusterListStart != 0xffffffffu)
    {
        return true;
    }

    // Before promotion every stored index still lives in the 16-entry packet.
    // Once a list exists the early return above owns subsequent appends.
    if (runVisibleClusterCount >
        HIKARI_CLUSTER_CULL_VISIBLE_PACKET_CAPACITY)
    {
        return false;
    }

    runClusterListStart = HikariClusterCullReserveVisibleClusterListRun();
    if (runClusterListStart == 0xffffffffu)
    {
        return false;
    }

    [unroll]
    for (uint slot = 0u;
         slot < HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY;
         ++slot)
    {
        if (slot >= runVisibleClusterCount)
        {
            continue;
        }

        const uint clusterIndex = runMergedGapCount == 0u
            ? runFirstCluster + slot
            : HikariClusterCullGetPacketIndex(
                packetClusterIndices0,
                packetClusterIndices1,
                packetClusterIndices2,
                packetClusterIndices3,
                slot);
        HikariClusterCullSetVisibleClusterListIndex(
            runClusterListStart,
            slot,
            clusterIndex);
    }

    return true;
}

void HikariClusterCullEmitDraw(
    ClusterCullInput input,
    uint firstCluster,
    uint clusterCount,
    uint visibleClusterCount,
    uint firstIndex,
    uint indexCount,
    uint mergedGapCount,
    uint mergedGapIndexCount,
    uint clusterListStart,
    uint4 packetClusterIndices0,
    uint4 packetClusterIndices1,
    uint4 packetClusterIndices2,
    uint4 packetClusterIndices3)
{
    if (mergedGapCount != 0u)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_COUNT,
            mergedGapCount);
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_MERGED_GAP_INDEX_COUNT,
            mergedGapIndexCount);
    }

    uint bucket = HikariClusterCullResolveBucket(input.flags);
    uint aggregateDrawCounterOffset =
        HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_COUNT + bucket * 4u;
    uint aggregateOverflowCounterOffset =
        HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_OVERFLOW_COUNT + bucket * 4u;
    uint drawCounterOffset =
        HikariClusterCullPassDrawCounterOffset(input.passKind, bucket);
    uint overflowCounterOffset =
        HikariClusterCullPassDrawOverflowCounterOffset(input.passKind, bucket);
    uint drawIndex = 0;
    gClusterCullCounters.InterlockedAdd(aggregateDrawCounterOffset, 1);
    gClusterCullCounters.InterlockedAdd(drawCounterOffset, 1, drawIndex);
    if (drawIndex >= gClusterCullCandidateCommandBucketCapacity)
    {
        gClusterCullCounters.InterlockedAdd(overflowCounterOffset, 1);
        gClusterCullCounters.InterlockedAdd(aggregateOverflowCounterOffset, 1);
        return;
    }

    uint globalDrawIndex =
        HikariClusterCullPassBucketBaseIndex(input.passKind, bucket) + drawIndex;
    uint visibleIndex = globalDrawIndex;
    if (globalDrawIndex >= gClusterCullCandidateCommandCapacity ||
        visibleIndex >= gClusterCullVisibleRangeCapacity)
    {
        gClusterCullCounters.InterlockedAdd(overflowCounterOffset, 1);
        gClusterCullCounters.InterlockedAdd(aggregateOverflowCounterOffset, 1);
        return;
    }

    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_RANGE_COUNT,
        1);
    if (gClusterCullEnableDebugCounters != 0u)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_COUNT,
            visibleClusterCount);
    }
    ClusterCullVisibleRange visible;
    visible.gpuSceneInstanceIndex = input.gpuSceneInstanceIndex;
    visible.clusterGeometrySrvDescriptorIndex = input.clusterGeometrySrvDescriptorIndex;
    visible.firstCluster = firstCluster;
    visible.clusterCount = clusterCount;
    visible.clusterSurfaceIndex = input.clusterSurfaceIndex;
    visible.passKind = input.passKind;
    visible.flags = input.flags;
    visible.clusterGeometryMetadataSrvDescriptorIndex = input.clusterGeometryMetadataSrvDescriptorIndex;
    visible.lodIndex = input.lodIndex;
    visible.pageIndex = input.firstPage;
    visible.drawBucket = bucket;
    visible.sectionIndex = input.sectionIndex;
    visible.clusterOffsetBytes = input.clusterOffsetBytes;
    visible.vertexOffsetBytes = input.vertexOffsetBytes;
    visible.vertexCount = input.vertexCount;
    visible.meshletPrimitiveOffsetBytes = input.meshletPrimitiveOffsetBytes;
    visible.meshletPrimitiveCount = input.meshletPrimitiveCount;
    visible.geometryClusterCount = input.geometryClusterCount;
    const bool amplificationFineCull =
        gClusterCullMeshletFineCullingOwner ==
            HIKARI_CLUSTER_CULL_MESHLET_FINE_CULL_AMPLIFICATION;
    const bool contiguousVisibleRange =
        mergedGapCount == 0u &&
        visibleClusterCount == clusterCount;
    uint visibleRangeFlags = amplificationFineCull
        ? HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_AS_FINE_CULL
        : (contiguousVisibleRange
            ? HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_PRECULLED
            : 0u);
    const bool useClusterList =
        !amplificationFineCull &&
        clusterListStart != 0xffffffffu &&
        visibleClusterCount <= HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY;
    const bool usePacket =
        !amplificationFineCull &&
        !useClusterList &&
        visibleClusterCount <= HIKARI_CLUSTER_CULL_VISIBLE_PACKET_CAPACITY;
    if (visibleClusterCount != 0u && usePacket)
    {
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_PACKET_RANGE_COUNT,
            1u);
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_PACKET_CLUSTER_COUNT,
            visibleClusterCount);
    }
    if (useClusterList)
    {
        visibleRangeFlags |=
            HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_PRECULLED |
            HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_CLUSTER_LIST;
    }
    else if (usePacket)
    {
        visibleRangeFlags |=
            HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_PRECULLED |
            HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_PACKET;
    }
    visible.reserved0 = visibleRangeFlags;
    uint visiblePayloadCount = 0u;
    if (useClusterList || usePacket)
    {
        visiblePayloadCount = visibleClusterCount;
    }
    visible.reserved1 = visiblePayloadCount;
    visible.packetClusterIndices0 = useClusterList
        ? uint4(clusterListStart, 0u, 0u, 0u)
        : packetClusterIndices0;
    visible.packetClusterIndices1 = packetClusterIndices1;
    visible.packetClusterIndices2 = packetClusterIndices2;
    visible.packetClusterIndices3 = packetClusterIndices3;
    gClusterCullVisibleRanges[visibleIndex] = visible;

}

void HikariClusterCullFlushVisibleRun(
    ClusterCullInput input,
    inout bool hasRun,
    inout uint runFirstCluster,
    inout uint runClusterCount,
    inout uint runVisibleClusterCount,
    inout uint runFirstIndex,
    inout uint runIndexCount,
    inout uint runMergedGapCount,
    inout uint runMergedGapIndexCount,
    inout uint runClusterListStart,
    inout uint4 runPacketClusterIndices0,
    inout uint4 runPacketClusterIndices1,
    inout uint4 runPacketClusterIndices2,
    inout uint4 runPacketClusterIndices3)
{
    if (!hasRun)
    {
        return;
    }

    HikariClusterCullEmitDraw(
        input,
        runFirstCluster,
        runClusterCount,
        runVisibleClusterCount,
        runFirstIndex,
        runIndexCount,
        runMergedGapCount,
        runMergedGapIndexCount,
        runClusterListStart,
        runPacketClusterIndices0,
        runPacketClusterIndices1,
        runPacketClusterIndices2,
        runPacketClusterIndices3);

    hasRun = false;
    runFirstCluster = 0u;
    runClusterCount = 0u;
    runVisibleClusterCount = 0u;
    runFirstIndex = 0u;
    runIndexCount = 0u;
    runMergedGapCount = 0u;
    runMergedGapIndexCount = 0u;
    runClusterListStart = 0xffffffffu;
    runPacketClusterIndices0 = HikariClusterCullEmptyPacket4();
    runPacketClusterIndices1 = HikariClusterCullEmptyPacket4();
    runPacketClusterIndices2 = HikariClusterCullEmptyPacket4();
    runPacketClusterIndices3 = HikariClusterCullEmptyPacket4();
}

void HikariClusterCullAppendVisibleCluster(
    ClusterCullInput input,
    uint clusterIndex,
    HikariMeshCluster cluster,
    inout bool hasRun,
    inout uint runFirstCluster,
    inout uint runClusterCount,
    inout uint runVisibleClusterCount,
    inout uint runFirstIndex,
    inout uint runIndexCount,
    inout uint runMergedGapCount,
    inout uint runMergedGapIndexCount,
    inout uint runClusterListStart,
    inout uint4 runPacketClusterIndices0,
    inout uint4 runPacketClusterIndices1,
    inout uint4 runPacketClusterIndices2,
    inout uint4 runPacketClusterIndices3)
{
    if (!hasRun)
    {
        hasRun = true;
        runFirstCluster = clusterIndex;
        runClusterCount = 1u;
        runVisibleClusterCount = 1u;
        runFirstIndex = cluster.firstIndex;
        runIndexCount = cluster.indexCount;
        runMergedGapCount = 0u;
        runMergedGapIndexCount = 0u;
        runClusterListStart = 0xffffffffu;
        runPacketClusterIndices0 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices1 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices2 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices3 = HikariClusterCullEmptyPacket4();
        HikariClusterCullSetPacketIndex(
            runPacketClusterIndices0,
            runPacketClusterIndices1,
            runPacketClusterIndices2,
            runPacketClusterIndices3,
            0u,
            clusterIndex);
        return;
    }

    uint runEndCluster = runFirstCluster + runClusterCount;
    uint runEndIndex = runFirstIndex + runIndexCount;
    bool clusterForward = clusterIndex >= runEndCluster;
    uint clusterGap = clusterForward ? clusterIndex - runEndCluster : 0xffffffffu;
    bool indexForward = cluster.firstIndex >= runEndIndex;
    uint indexGap = indexForward ? cluster.firstIndex - runEndIndex : 0xffffffffu;
    uint clusterEndIndex = cluster.firstIndex + cluster.indexCount;
    uint mergedFirstIndex = min(runFirstIndex, cluster.firstIndex);
    uint mergedEndIndex = max(runEndIndex, clusterEndIndex);
    uint mergedIndexCount = mergedEndIndex >= mergedFirstIndex
        ? mergedEndIndex - mergedFirstIndex
        : 0xffffffffu;
    uint mergedClusterCount = clusterIndex >= runFirstCluster
        ? clusterIndex - runFirstCluster + 1u
        : 0xffffffffu;

    // 蜷御ｸ surface/page 蜀・・蟆上＆縺ｪ谺縺代□縺代ｒ蜷ｸ蜿弱＠縲∫ｴｰ縺九☆縺弱ｋ draw args 繧・GPU 蛛ｴ縺ｧ蝨ｧ邵ｮ縺吶ｋ縲・
    bool contiguousMerge = runMergedGapCount == 0u && clusterGap == 0u && indexGap == 0u;
    const uint nextVisibleClusterCount = runVisibleClusterCount + 1u;
    const bool hasMergeGap = clusterGap != 0u || !indexForward || indexGap != 0u;
    const bool fitsPacket =
        nextVisibleClusterCount <= HIKARI_CLUSTER_CULL_VISIBLE_PACKET_CAPACITY;
    const bool fitsClusterList =
        nextVisibleClusterCount <= HIKARI_CLUSTER_CULL_VISIBLE_CLUSTER_LIST_PACK_CAPACITY;
    const bool gapIndexWithinBudget =
        indexForward &&
        indexGap <= HikariClusterCullMergeGapIndexLimit() &&
        runMergedGapIndexCount + indexGap <= HikariClusterCullMergeRunGapBudget();
    const bool legacyCompactGapMerge =
        hasMergeGap &&
        gapIndexWithinBudget &&
        (fitsPacket || fitsClusterList);
    const bool meshletPreciseCompaction =
        gClusterCullMeshletFineCullingOwner ==
            HIKARI_CLUSTER_CULL_MESHLET_FINE_CULL_COMPUTE;
    const bool meshletPreciseGapMerge =
        meshletPreciseCompaction &&
        hasMergeGap &&
        fitsClusterList;
    const bool legacyIndexSpanValid =
        mergedIndexCount <= HikariClusterCullMergeMaxIndexSpan();
    bool canMerge =
        clusterForward &&
        ((indexForward &&
            (contiguousMerge ||
                (legacyIndexSpanValid && legacyCompactGapMerge))) ||
            meshletPreciseGapMerge);
    if (canMerge &&
        meshletPreciseCompaction &&
        !fitsClusterList)
    {
        canMerge = false;
    }
    if (canMerge &&
        meshletPreciseCompaction &&
        !fitsPacket)
    {
        canMerge = HikariClusterCullPromoteVisibleRunToClusterList(
            runClusterListStart,
            runFirstCluster,
            runVisibleClusterCount,
            runMergedGapCount,
            runPacketClusterIndices0,
            runPacketClusterIndices1,
            runPacketClusterIndices2,
            runPacketClusterIndices3);
    }
    if (!canMerge)
    {
        HikariClusterCullFlushVisibleRun(
            input,
            hasRun,
            runFirstCluster,
            runClusterCount,
            runVisibleClusterCount,
            runFirstIndex,
            runIndexCount,
            runMergedGapCount,
            runMergedGapIndexCount,
            runClusterListStart,
            runPacketClusterIndices0,
            runPacketClusterIndices1,
            runPacketClusterIndices2,
            runPacketClusterIndices3);
        hasRun = true;
        runFirstCluster = clusterIndex;
        runClusterCount = 1u;
        runVisibleClusterCount = 1u;
        runFirstIndex = cluster.firstIndex;
        runIndexCount = cluster.indexCount;
        runMergedGapCount = 0u;
        runMergedGapIndexCount = 0u;
        runClusterListStart = 0xffffffffu;
        runPacketClusterIndices0 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices1 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices2 = HikariClusterCullEmptyPacket4();
        runPacketClusterIndices3 = HikariClusterCullEmptyPacket4();
        HikariClusterCullSetPacketIndex(
            runPacketClusterIndices0,
            runPacketClusterIndices1,
            runPacketClusterIndices2,
            runPacketClusterIndices3,
            0u,
            clusterIndex);
        return;
    }

    if (runVisibleClusterCount < HIKARI_CLUSTER_CULL_VISIBLE_PACKET_CAPACITY)
    {
        HikariClusterCullSetPacketIndex(
            runPacketClusterIndices0,
            runPacketClusterIndices1,
            runPacketClusterIndices2,
            runPacketClusterIndices3,
            runVisibleClusterCount,
            clusterIndex);
    }
    if (runClusterListStart != 0xffffffffu)
    {
        HikariClusterCullSetVisibleClusterListIndex(
            runClusterListStart,
            runVisibleClusterCount,
            clusterIndex);
    }
    runClusterCount = mergedClusterCount;
    ++runVisibleClusterCount;
    runFirstIndex = mergedFirstIndex;
    runIndexCount = mergedIndexCount;
    if (clusterGap != 0u || !indexForward || indexGap != 0u)
    {
        ++runMergedGapCount;
        runMergedGapIndexCount += indexForward ? indexGap : 0u;
    }
}

bool HikariClusterCullIsGpuSceneCandidate(HikariSurfaceGpuSceneInstance instance)
{
    static const uint requiredResources =
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY |
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SHADER_VISIBLE |
        HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_SURFACE_RANGE;

    const bool transparent =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT) != 0u;
    const bool depthAware =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DEPTH_AWARE) != 0u;

    if (gClusterCullPassKind == HIKARI_CLUSTER_CULL_PASS_FORWARD_OPAQUE)
    {
        if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_OPAQUE) == 0u ||
            transparent ||
            depthAware)
        {
            return false;
        }
    }
    else if (gClusterCullPassKind == HIKARI_CLUSTER_CULL_PASS_DEPTH_PREPASS)
    {
        if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_DEPTH_PREPASS) == 0u ||
            transparent ||
            depthAware)
        {
            return false;
        }
    }
    else if (gClusterCullPassKind == HIKARI_CLUSTER_CULL_PASS_SHADOW)
    {
        if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_SHADOW) == 0u ||
            transparent ||
            depthAware)
        {
            return false;
        }
    }
    else if (gClusterCullPassKind == HIKARI_CLUSTER_CULL_PASS_FORWARD_DEPTH_AWARE)
    {
        if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_DEPTH_AWARE) == 0u ||
            !depthAware)
        {
            return false;
        }
    }
    else if (gClusterCullPassKind == HIKARI_CLUSTER_CULL_PASS_FORWARD_TRANSPARENT)
    {
        if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_PASS_FORWARD_TRANSPARENT) == 0u ||
            !transparent ||
            depthAware)
        {
            return false;
        }
    }
    else
    {
        if (transparent || depthAware)
        {
            return false;
        }
    }

    return
        instance.geometryBackend == HIKARI_SURFACE_GEOMETRY_BACKEND_CLUSTER_GEOMETRY &&
        (instance.resourceFlags & requiredResources) == requiredResources &&
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_CLUSTER_MAINLINE) != 0u &&
        instance.materialDataIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterGeometrySrvDescriptorIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterGeometryMetadataSrvDescriptorIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterRangeIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterRangeCount > 0u &&
        instance.clusterSurfaceIndex != HIKARI_CLUSTER_GEOMETRY_INVALID_INDEX &&
        instance.clusterIndexCount > 0u;
}

bool HikariClusterCullLodRangeUsable(
    HikariClusterGeometryHeader header,
    HikariClusterGeometrySurfaceLodRange range,
    uint surfaceIndex,
    uint sectionIndex)
{
    return
        range.surfaceIndex == surfaceIndex &&
        range.sectionIndex == sectionIndex &&
        range.clusterCount > 0u &&
        range.indexCount > 0u &&
        range.pageCount > 0u &&
        range.firstCluster < header.clusterCount &&
        range.firstPage < header.pageCount &&
        range.firstCluster + range.clusterCount <= header.clusterCount &&
        range.firstPage + range.pageCount <= header.pageCount;
}

float4 HikariClusterCullResolveSectionLodWorldSphere(
    HikariSurfaceGpuSceneInstance instance,
    HikariClusterGeometrySurfaceSection section,
    float4 sectionWorldSphere)
{
    if (section.lodMetricCenterRadius.w <= 0.000001f)
    {
        return sectionWorldSphere;
    }

    return HikariClusterCullBuildWorldSphere(
        instance.clusterWorld,
        section.lodMetricCenterRadius,
        section.boundsMin,
        section.boundsMax);
}

float HikariClusterCullResolveProjectedErrorBudget()
{
    return max(gClusterCullLodTargetErrorNdc, 0.0f);
}

float HikariClusterCullResolveSectionErrorBudget(
    HikariClusterGeometrySurfaceSection section,
    HikariClusterGeometrySurfaceLodRange range)
{
    float baseBudget = max(section.lodErrorBudgetNdc, HikariClusterCullResolveProjectedErrorBudget());
    float lodRelax =
        1.0f +
        min((float)range.lodIndex, 4.0f) *
        max(gClusterCullLodErrorRelaxPerLevel, 0.0f);
    return max(baseBudget * lodRelax, 0.0f);
}

float HikariClusterCullResolveLodTransitionRadius(
    HikariClusterGeometrySurfaceLodRange currentRange,
    HikariClusterGeometrySurfaceLodRange candidateRange)
{
    float baseRadius = max(currentRange.minScreenRadius, candidateRange.minScreenRadius);
    float laterLevel = max((float)candidateRange.lodIndex - 1.0f, 0.0f);
    float relax =
        1.0f +
        laterLevel *
        max(gClusterCullLodTransitionRelaxPerLevel, 0.0f);
    return max(baseRadius * relax, 0.0f);
}

bool HikariClusterCullSelectSectionLodRange(
    ByteAddressBuffer geometry,
    HikariClusterGeometryHeader header,
    HikariSurfaceGpuSceneInstance instance,
    HikariClusterGeometrySurfaceSection section,
    float4 sectionWorldSphere,
    out HikariClusterGeometrySurfaceLodRange selectedRange)
{
    selectedRange = (HikariClusterGeometrySurfaceLodRange)0;
    if ((instance.resourceFlags & HIKARI_SURFACE_GPU_SCENE_RESOURCE_CLUSTER_GEOMETRY_LOD_RANGES) == 0u ||
        section.lodRangeCount == 0u ||
        section.firstLodRange >= header.surfaceLodRangeCount)
    {
        return false;
    }

    uint rangeBegin = section.firstLodRange;
    uint rangeEnd = min(rangeBegin + section.lodRangeCount, header.surfaceLodRangeCount);
    float4 lodWorldSphere =
        HikariClusterCullResolveSectionLodWorldSphere(instance, section, sectionWorldSphere);
    float screenRadius = HikariClusterCullProjectedScreenRadius(lodWorldSphere);
    float targetProjectedError = HikariClusterCullResolveProjectedErrorBudget();
    bool useProjectedError =
        gClusterCullEnableLodErrorSelection != 0u &&
        targetProjectedError > 0.0f;
    bool hasFallback = false;

    for (uint rangeIndex = rangeBegin; rangeIndex < rangeEnd; ++rangeIndex)
    {
        HikariClusterGeometrySurfaceLodRange range =
            HikariLoadClusterGeometrySurfaceLodRange(geometry, header, rangeIndex);
        if (!HikariClusterCullLodRangeUsable(
                header,
                range,
                instance.clusterSurfaceIndex,
                section.sectionIndex))
        {
            continue;
        }

        if (!hasFallback)
        {
            selectedRange = range;
            hasFallback = true;
            continue;
        }

        // LOD 縺ｮ谿ｵ髫主､画峩縺ｯ隕九◆逶ｮ繧ｵ繧､繧ｺ縺ｨ蟷ｾ菴戊ｪ､蟾ｮ縺ｮ荳｡譁ｹ縺ｧ豎ｺ繧√ｋ縲・
        // 迚・婿縺縺代〒關ｽ縺ｨ縺吶→縲・ｫ伜ｯ・ｺｦ繧ｭ繝｣繝ｩ繧ｯ繧ｿ繝ｼ縺碁□霍晞屬縺ｧ譛邨・LOD 縺ｫ蠑ｵ繧贋ｻ倥″繧・☆縺・・
        float transitionRadius =
            HikariClusterCullResolveLodTransitionRadius(selectedRange, range);
        bool radiusAllowsStepDown =
            screenRadius < max(transitionRadius, 0.0f);
        bool errorAllowsStepDown =
            !useProjectedError ||
            HikariClusterCullProjectedLodError(lodWorldSphere, range) <=
                HikariClusterCullResolveSectionErrorBudget(section, range);
        if (!radiusAllowsStepDown || !errorAllowsStepDown)
        {
            break;
        }

        selectedRange = range;
    }

    return hasFallback;
}

void HikariClusterCullRecordSelectedLod(uint lodIndex)
{
    uint counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD3_PLUS_SELECTED_COUNT;
    if (lodIndex == 0u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD0_SELECTED_COUNT;
    }
    else if (lodIndex == 1u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD1_SELECTED_COUNT;
    }
    else if (lodIndex == 2u)
    {
        counterOffset = HIKARI_CLUSTER_CULL_COUNTER_LOD2_SELECTED_COUNT;
    }
    gClusterCullCounters.InterlockedAdd(counterOffset, 1);
}

ClusterCullInput HikariClusterCullBuildInput(
    uint surfaceGpuSceneIndex,
    HikariSurfaceGpuSceneInstance instance,
    HikariClusterGeometryHeader header,
    HikariClusterGeometrySurfaceLodRange lodRange,
    float4 boundsCenterRadius)
{
    ClusterCullInput input = (ClusterCullInput)0;
    input.clusterWorld = instance.clusterWorld;
    input.boundsCenterRadius = boundsCenterRadius;
    input.gpuSceneInstanceIndex = surfaceGpuSceneIndex;
    input.clusterGeometrySrvDescriptorIndex = instance.clusterGeometrySrvDescriptorIndex;
    input.clusterGeometryMetadataSrvDescriptorIndex = instance.clusterGeometryMetadataSrvDescriptorIndex;
    input.firstCluster = lodRange.firstCluster;
    input.clusterCount = lodRange.clusterCount;
    input.clusterSurfaceIndex = instance.clusterSurfaceIndex;
    input.passKind = gClusterCullPassKind;
    input.flags = instance.flags;
    input.clusterIndexCount = lodRange.indexCount;
    input.firstPage = lodRange.firstPage;
    input.pageCount = lodRange.pageCount;
    input.pageTaskBaseIndex = 0u;
    input.lodIndex = lodRange.lodIndex;
    input.sectionIndex = lodRange.sectionIndex;
    input.clusterOffsetBytes = header.clusterOffsetBytes;
    input.vertexOffsetBytes = header.vertexOffsetBytes;
    input.vertexCount = header.vertexCount;
    input.meshletPrimitiveOffsetBytes = header.meshletPrimitiveOffsetBytes;
    input.meshletPrimitiveCount = header.meshletPrimitiveCount;
    input.geometryClusterCount = header.clusterCount;
    return input;
}

ClusterCullInput HikariClusterCullBuildInputFromPageTask(ClusterCullPageTask task)
{
    ClusterCullInput input = (ClusterCullInput)0;
    input.clusterWorld =
        HikariGetSurfaceGpuSceneInstanceAt(task.gpuSceneInstanceIndex).clusterWorld;
    input.boundsCenterRadius = float4(0.0f, 0.0f, 0.0f, 0.0f);
    input.gpuSceneInstanceIndex = task.gpuSceneInstanceIndex;
    input.clusterGeometrySrvDescriptorIndex = task.clusterGeometrySrvDescriptorIndex;
    input.clusterGeometryMetadataSrvDescriptorIndex = task.clusterGeometryMetadataSrvDescriptorIndex;
    input.firstCluster = task.firstCluster;
    input.clusterCount = task.endCluster > task.firstCluster
        ? task.endCluster - task.firstCluster
        : 0u;
    input.clusterSurfaceIndex = task.clusterSurfaceIndex;
    input.passKind = task.passKind;
    input.flags = task.flags;
    input.clusterIndexCount = 0u;
    input.firstPage = task.pageIndex;
    input.pageCount = 1u;
    input.pageTaskBaseIndex = 0u;
    input.lodIndex = task.lodIndex;
    input.sectionIndex = task.sectionIndex;
    input.clusterOffsetBytes = task.clusterOffsetBytes;
    input.vertexOffsetBytes = task.vertexOffsetBytes;
    input.vertexCount = task.vertexCount;
    input.meshletPrimitiveOffsetBytes = task.meshletPrimitiveOffsetBytes;
    input.meshletPrimitiveCount = task.meshletPrimitiveCount;
    input.geometryClusterCount = task.geometryClusterCount;
    return input;
}

void HikariClusterCullEmitPageTasks(
    ClusterCullInput input,
    uint firstCluster,
    uint endCluster,
    uint firstPage,
    uint endPage)
{
    uint pageCount = endPage > firstPage ? endPage - firstPage : 0u;
    if (pageCount == 0u)
    {
        return;
    }

    uint pageTaskGroupSize = HikariClusterCullPageTaskGroupSize();
    uint groupCount = (pageCount + pageTaskGroupSize - 1u) / pageTaskGroupSize;
    uint taskBase = 0u;
    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT,
        groupCount,
        taskBase);
    if (taskBase >= gClusterCullPageTaskCapacity)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT,
            groupCount);
        return;
    }

    uint writableCount = min(groupCount, gClusterCullPageTaskCapacity - taskBase);
    if (writableCount < groupCount)
    {
        gClusterCullCounters.InterlockedAdd(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT,
            groupCount - writableCount);
    }

    for (uint groupOffset = 0u; groupOffset < writableCount; ++groupOffset)
    {
        uint pageOffset = groupOffset * pageTaskGroupSize;
        uint groupPageCount = min(pageTaskGroupSize, pageCount - pageOffset);
        ClusterCullPageTask task = (ClusterCullPageTask)0;
        task.gpuSceneInstanceIndex = input.gpuSceneInstanceIndex;
        task.clusterGeometrySrvDescriptorIndex = input.clusterGeometrySrvDescriptorIndex;
        task.clusterGeometryMetadataSrvDescriptorIndex = input.clusterGeometryMetadataSrvDescriptorIndex;
        task.firstCluster = firstCluster;
        task.endCluster = endCluster;
        task.clusterSurfaceIndex = input.clusterSurfaceIndex;
        task.passKind = input.passKind;
        task.flags = input.flags;
        task.pageIndex = firstPage + pageOffset;
        task.lodIndex = input.lodIndex;
        task.sectionIndex = input.sectionIndex;
        task.clusterOffsetBytes = input.clusterOffsetBytes;
        task.vertexOffsetBytes = input.vertexOffsetBytes;
        task.vertexCount = input.vertexCount;
        task.meshletPrimitiveOffsetBytes = input.meshletPrimitiveOffsetBytes;
        task.meshletPrimitiveCount = input.meshletPrimitiveCount;
        task.geometryClusterCount = input.geometryClusterCount;
        task.reserved0 = groupPageCount;
        task.reserved1 = 0u;
        task.reserved2 = 0u;
        gClusterCullPageTasks[taskBase + groupOffset] = task;
    }
}



#endif
