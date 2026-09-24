#ifndef HIKARI_CLUSTER_CULL_COMMON_INCLUDED
#define HIKARI_CLUSTER_CULL_COMMON_INCLUDED

#include "Include/Cluster/HIKARI_ClusterCullContract.hlsli"
#include "Include/Cluster/HIKARI_ClusterCullingMath.hlsli"

bool HikariClusterCullSphereVisible(float4 boundsCenterRadius)
{
    return gClusterCullEnableFrustumCull == 0u ||
        HikariClusterSphereVisible(gClusterCullViewProj, boundsCenterRadius);
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
            length(HikariClusterMatrixRow0(viewProj).xyz),
            length(HikariClusterMatrixRow1(viewProj).xyz));
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

#include "Include/Depth/HIKARI_DepthOcclusion.hlsli"
typedef HikariDepthOcclusionQuery HikariClusterCullHzbQuery;

uint HikariClusterCullBuildHzbQuery(float4x4 world, float4 boundsMin, float4 boundsMax,
    out HikariClusterCullHzbQuery query)
{
    bool valid = HikariBuildDepthOcclusionQuery(gClusterCullHzbViewProj, world,
        boundsMin.xyz, boundsMax.xyz, uint2(gClusterCullHzbWidth, gClusterCullHzbHeight), query);
    if (query.maxExtentPixels > max(gClusterCullHzbMaxScreenRadiusPixels, 1.0f))
        query.flags |= HIKARI_CLUSTER_CULL_HZB_QUERY_FLAG_LARGE_RECT;
    if (valid) return HIKARI_CLUSTER_CULL_HZB_QUERY_OK;
    if (query.rejectReason == 2u) return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE;
    if (query.rejectReason == 3u) return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN;
    return HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID;
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

        HikariClusterCullHistoryResetVisible(occlusionKey);
        return false;
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
    bool rawOccluded = HikariDepthOccluded(gClusterCullHzb, query,
        uint2(gClusterCullHzbWidth, gClusterCullHzbHeight), gClusterCullHzbMipCount,
        gClusterCullHzbDepthBias);
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

uint HikariClusterCullResolveBucket(uint flags)
{
    return (flags & HIKARI_SURFACE_GPU_SCENE_FLAG_DOUBLE_SIDED) != 0u
        ? HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED
        : HIKARI_CLUSTER_DRAW_BUCKET_BACK_FACE;
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

void HikariClusterCullEmitDraw(ClusterCullInput input, uint firstCluster,
    uint clusterCount, uint firstIndex, uint indexCount)
{
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
            clusterCount);
    }
    ClusterCullVisibleRange visible = (ClusterCullVisibleRange)0;
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
    visible.reserved0 = HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_AS_FINE_CULL;
    gClusterCullVisibleRanges[visibleIndex] = visible;
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

    return HikariClusterBuildWorldSphere(
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
    const bool cameraCenteredOcean =
        (instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_CAMERA_CENTERED_OCEAN) != 0u;

    // Camera-centered ocean geometry is evaluated from the snapped GridPlane
    // lattice and graded in the deformation route.  Its authored section
    // sphere is intentionally finite and therefore cannot drive projected
    // error, transition radius, occlusion, or LOD selection.  Keep the
    // finest authored range (LOD0) whenever available; the first usable range
    // is only a malformed/legacy-data fallback.
    if (cameraCenteredOcean)
    {
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
            }
            if (range.lodIndex == 0u)
            {
                selectedRange = range;
                return true;
            }
        }
        return hasFallback;
    }

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

ClusterCullInput HikariClusterCullBuildInputFromPageTask(ClusterCullPageTask task,
    HikariSurfaceGpuSceneInstance instance)
{
    ClusterCullInput input = (ClusterCullInput)0;
    input.clusterWorld = instance.clusterWorld;
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
