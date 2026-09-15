#include "HIKARI_ClusterCullCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

// 1 lane = 1 page。CS は粗い page 判定と AS の候補 range 発行だけを行う。
[numthreads(HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE, 1, 1)]
void CullPageTasksCS(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint taskIndex = (groupId.y * HIKARI_CLUSTER_CULL_PAGE_TASK_DISPATCH_MAX_X + groupId.x)
        * HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE + groupIndex;
    uint pageTaskCount = gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    if (taskIndex >= min(pageTaskCount, gClusterCullPageTaskCapacity)) return;
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

    HikariSurfaceGpuSceneInstance instance = HikariGetSurfaceGpuSceneInstanceAt(task.gpuSceneInstanceIndex);
    ClusterCullInput input = HikariClusterCullBuildInputFromPageTask(task, instance);
    const bool boundsValid = HikariClusterHasConservativeBounds(instance);

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

    // Page 単位の粗い剔除。各 lane は固有の page key を処理する。
    {

        HikariClusterCullAddDebugCounter(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TESTED_COUNT,
            1u);

        float4 pageWorldSphere = HikariClusterBuildCullSphere(
            instance,
            float4(0.0f, 0.0f, 0.0f, 0.0f),
            page.boundsMin,
            page.boundsMax);
        float4x4 cullWorld = instance.clusterWorld;
        float4 cullMin = page.boundsMin, cullMax = page.boundsMax;
        HikariClusterBuildCullBox(instance, cullWorld, cullMin, cullMax);
        if (boundsValid && (!HikariClusterCullSphereVisible(pageWorldSphere) ||
            (gClusterCullEnableFrustumCull != 0u && !HikariClusterBoxVisible(
                gClusterCullViewProj, cullWorld, cullMin.xyz, cullMax.xyz))))
        {
            HikariClusterCullAddDebugCounter(
                HIKARI_CLUSTER_CULL_COUNTER_PAGE_FRUSTUM_CULLED_COUNT,
                1u);
            return;
        }
        else if (boundsValid && HikariClusterCullShouldTestPageHzb(pageWorldSphere))
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
                cullWorld,
                cullMin,
                cullMax,
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
                return;
            }
        }
    }
    // meshlet の読み込み、frustum/cone/HZB と圧縮は AS が所有する。
    const uint count = pageRangeEnd - pageRangeStart;
    HikariClusterCullEmitDraw(input, pageRangeStart, count, page.firstIndex, page.indexCount);
}
