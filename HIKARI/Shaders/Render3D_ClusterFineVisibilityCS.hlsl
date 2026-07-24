#include "HIKARI_ClusterCullCommon.hlsli"

// 1 thread group = 1 page task (= 1 page, 最大 64 cluster)。
// cluster 単位の重いテスト (frustum / cone / HZB) を 64 thread で並列に行い、
// run-merge / draw emit の逐次ロジックは LDS に載せた結果を thread0 が
// 走査して従来と同一のセマンティクスで実行する。
groupshared uint gClusterCullLdsPageCulled;
groupshared uint gClusterCullLdsVisibleFlag[HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE];
groupshared uint gClusterCullLdsFirstIndex[HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE];
groupshared uint gClusterCullLdsIndexCount[HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE];

[numthreads(HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE, 1, 1)]
void CullPageTasksCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint taskIndex =
        groupId.y * HIKARI_CLUSTER_CULL_PAGE_TASK_DISPATCH_MAX_X + groupId.x;
    uint pageTaskCount =
        gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    uint clampedTaskCount = min(pageTaskCount, gClusterCullPageTaskCapacity);
    if (taskIndex >= clampedTaskCount)
    {
        return;
    }

    ClusterCullPageTask task = gClusterCullPageTasks[taskIndex];
    if (task.clusterGeometryMetadataSrvDescriptorIndex < gClusterCullClusterSrvPoolBegin)
    {
        return;
    }

    uint clusterMetadataPoolIndex =
        task.clusterGeometryMetadataSrvDescriptorIndex - gClusterCullClusterSrvPoolBegin;
    if (clusterMetadataPoolIndex >= gClusterCullClusterSrvPoolCount)
    {
        return;
    }

    ByteAddressBuffer metadata =
        gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(metadata);
    if (!HikariIsValidClusterGeometryHeader(header) ||
        task.firstCluster >= task.endCluster ||
        task.firstCluster >= header.clusterCount ||
        task.clusterSurfaceIndex >= header.surfaceCount ||
        task.pageIndex >= header.pageCount)
    {
        return;
    }

    ClusterCullInput input = HikariClusterCullBuildInputFromPageTask(task);
    const bool vertexDeformed =
        (input.flags & (
            HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX)) != 0u;

    HikariClusterPage page = HikariLoadClusterPage(metadata, header, task.pageIndex);
    uint pageEndCluster = page.firstCluster + page.clusterCount;
    uint pageRangeStart = max(page.firstCluster, task.firstCluster);
    uint pageRangeEnd = min(pageEndCluster, min(task.endCluster, header.clusterCount));
    if (page.indexCount == 0u ||
        page.clusterCount == 0u ||
        pageRangeStart >= pageRangeEnd)
    {
        return;
    }

    // Page 単位の粗い剔除。HZB テストは occlusion history への書き込み副作用
    // を持つため thread0 のみで実行し、結果を LDS で共有する。
    if (groupIndex == 0u)
    {
        gClusterCullLdsPageCulled = 0u;
        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TESTED_COUNT,
            1u);

        float4 pageWorldSphere = HikariClusterCullBuildWorldSphere(
            input.clusterWorld,
            float4(0.0f, 0.0f, 0.0f, 0.0f),
            page.boundsMin,
            page.boundsMax);
        if (!vertexDeformed && !HikariClusterCullSphereVisible(pageWorldSphere))
        {
            HikariClusterCullAddDebugCounter(
                HIKARI_CLUSTER_CULL_COUNTER_PAGE_FRUSTUM_CULLED_COUNT,
                1u);
            gClusterCullLdsPageCulled = 1u;
        }
        else if (!vertexDeformed && HikariClusterCullShouldTestPageHzb(pageWorldSphere))
        {
            bool pageOcclusionTested = false;
            uint pageOcclusionKey = HikariClusterCullBuildOcclusionKey(
                input.gpuSceneInstanceIndex,
                input.passKind,
                input.lodIndex,
                input.sectionIndex,
                task.pageIndex,
                0xffffffffu,
                0u);
            bool pageOccluded = HikariClusterCullTryHzbOccluded(
                input.passKind,
                input.clusterWorld,
                page.boundsMin,
                page.boundsMax,
                pageOcclusionKey,
                false,
                pageOcclusionTested);
            if (pageOcclusionTested)
            {
                HikariClusterCullAddDebugCounter(
                    HIKARI_CLUSTER_CULL_COUNTER_PAGE_OCCLUSION_TESTED_COUNT,
                    1u);
            }
            if (pageOccluded)
            {
                HikariClusterCullAddDebugCounter(
                    HIKARI_CLUSTER_CULL_COUNTER_PAGE_OCCLUSION_CULLED_COUNT,
                    1u);
                gClusterCullLdsPageCulled = 1u;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gClusterCullLdsPageCulled != 0u)
    {
        return;
    }

    // Mesh Shader 主線では CS は page 単位の候補 Range だけを発行する。
    // meshlet の frustum / cone 精査と圧縮は AS の LDS / Wave 内で完結させる。
    if (gClusterCullMeshletFineCullingOwner ==
        HIKARI_CLUSTER_CULL_MESHLET_FINE_CULL_AMPLIFICATION)
    {
        if (groupIndex == 0u)
        {
            const uint candidateClusterCount = pageRangeEnd - pageRangeStart;
            HikariClusterCullEmitDraw(
                input,
                pageRangeStart,
                candidateClusterCount,
                candidateClusterCount,
                page.firstIndex,
                page.indexCount,
                0u,
                0u,
                0xffffffffu,
                HikariClusterCullEmptyPacket4(),
                HikariClusterCullEmptyPacket4(),
                HikariClusterCullEmptyPacket4(),
                HikariClusterCullEmptyPacket4());
        }
        return;
    }

    bool doubleSided = HikariClusterCullIsDoubleSided(input.flags);
    bool coneSkipMaterial =
        (input.flags & (
            HIKARI_SURFACE_GPU_SCENE_FLAG_ALPHA_MASKED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_TRANSPARENT |
            HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
            HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX)) != 0u;

    // run-merge の状態は thread0 のレジスタにのみ意味を持つ。
    bool hasRun = false;
    uint runFirstCluster = 0u;
    uint runClusterCount = 0u;
    uint runVisibleClusterCount = 0u;
    uint runFirstIndex = 0u;
    uint runIndexCount = 0u;
    uint runMergedGapCount = 0u;
    uint runMergedGapIndexCount = 0u;
    uint runClusterListStart = 0xffffffffu;
    uint4 runPacketClusterIndices0 = HikariClusterCullEmptyPacket4();
    uint4 runPacketClusterIndices1 = HikariClusterCullEmptyPacket4();
    uint4 runPacketClusterIndices2 = HikariClusterCullEmptyPacket4();
    uint4 runPacketClusterIndices3 = HikariClusterCullEmptyPacket4();

    for (uint chunkStart = pageRangeStart;
         chunkStart < pageRangeEnd;
         chunkStart += HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE)
    {
        uint slotClusterIndex = chunkStart + groupIndex;
        uint visibleFlag = 0u;
        uint visibleFirstIndex = 0u;
        uint visibleIndexCount = 0u;

        if (slotClusterIndex < pageRangeEnd)
        {
            HikariMeshCluster cluster =
                HikariLoadMeshCluster(metadata, header, slotClusterIndex);
            bool clusterValid =
                cluster.indexCount != 0u &&
                cluster.surfaceIndex == input.clusterSurfaceIndex &&
                cluster.firstIndex < header.indexCount &&
                cluster.firstIndex + cluster.indexCount <= header.indexCount;
            if (clusterValid)
            {
                HikariClusterCullAddDebugCounter(
                    HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_TESTED_COUNT,
                    1u);

                float4 clusterWorldSphere = HikariClusterCullBuildWorldSphere(
                    input.clusterWorld,
                    cluster.sphereCenterRadius,
                    cluster.boundsMin,
                    cluster.boundsMax);
                bool clusterVisible = true;
                if (!vertexDeformed && !HikariClusterCullSphereVisible(clusterWorldSphere))
                {
                    HikariClusterCullAddDebugCounter(
                        HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_FRUSTUM_CULLED_COUNT,
                        1u);
                    clusterVisible = false;
                }

                if (!vertexDeformed && clusterVisible &&
                    HikariClusterCullShouldTestClusterHzb(clusterWorldSphere))
                {
                    bool clusterOcclusionTested = false;
                    uint clusterOcclusionKey = HikariClusterCullBuildOcclusionKey(
                        input.gpuSceneInstanceIndex,
                        input.passKind,
                        input.lodIndex,
                        input.sectionIndex,
                        task.pageIndex,
                        slotClusterIndex,
                        1u);
                    bool clusterOccluded = HikariClusterCullTryHzbOccluded(
                        input.passKind,
                        input.clusterWorld,
                        cluster.boundsMin,
                        cluster.boundsMax,
                        clusterOcclusionKey,
                        true,
                        clusterOcclusionTested);
                    if (clusterOcclusionTested)
                    {
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_OCCLUSION_TESTED_COUNT,
                            1u);
                    }
                    if (clusterOccluded)
                    {
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_OCCLUSION_CULLED_COUNT,
                            1u);
                        clusterVisible = false;
                    }
                }

                if (clusterVisible)
                {
                    if (doubleSided)
                    {
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_CLUSTER_COUNT,
                            1u);
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_CONE_SKIPPED_DOUBLE_SIDED_COUNT,
                            1u);
                    }
                    else if (coneSkipMaterial)
                    {
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_CONE_SKIPPED_MATERIAL_COUNT,
                            1u);
                    }
                    else if (gClusterCullEnableConeCull != 0u)
                    {
                        HikariClusterCullAddDebugCounter(
                            HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_TESTED_COUNT,
                            1u);
                        if (HikariClusterCullConeBackfacing(
                            input.clusterWorld,
                            cluster,
                            clusterWorldSphere))
                        {
                            HikariClusterCullAddDebugCounter(
                                HIKARI_CLUSTER_CULL_COUNTER_CLUSTER_CONE_CULLED_COUNT,
                                1u);
                            clusterVisible = false;
                        }
                    }
                }

                if (clusterVisible)
                {
                    visibleFlag = 1u;
                    visibleFirstIndex = cluster.firstIndex;
                    visibleIndexCount = cluster.indexCount;
                }
            }
        }

        gClusterCullLdsVisibleFlag[groupIndex] = visibleFlag;
        gClusterCullLdsFirstIndex[groupIndex] = visibleFirstIndex;
        gClusterCullLdsIndexCount[groupIndex] = visibleIndexCount;
        GroupMemoryBarrierWithGroupSync();

        if (groupIndex == 0u)
        {
            uint chunkCount =
                min(pageRangeEnd - chunkStart, HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE);
            for (uint slot = 0u; slot < chunkCount; ++slot)
            {
                if (gClusterCullLdsVisibleFlag[slot] == 0u)
                {
                    continue;
                }
                HikariMeshCluster runCluster = (HikariMeshCluster)0;
                runCluster.firstIndex = gClusterCullLdsFirstIndex[slot];
                runCluster.indexCount = gClusterCullLdsIndexCount[slot];
                HikariClusterCullAppendVisibleCluster(
                    input,
                    chunkStart + slot,
                    runCluster,
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
            }
        }
        // 次 chunk の LDS 再利用前に thread0 の走査完了を保証する。
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u)
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
    }
}
