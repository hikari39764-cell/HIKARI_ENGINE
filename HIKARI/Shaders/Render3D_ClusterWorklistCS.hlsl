#include "HIKARI_ClusterCullCommon.hlsli"

[numthreads(64, 1, 1)]
void ExpandPageTasksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localInstanceIndex = dispatchThreadId.x;
    if (localInstanceIndex >= gClusterCullInputCount)
    {
        return;
    }

    uint surfaceGpuSceneIndex =
        gClusterCullSurfaceGpuSceneBaseIndex + localInstanceIndex;
    HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    if (!HikariClusterCullIsGpuSceneCandidate(instance))
    {
        return;
    }

    gClusterCullCounters.InterlockedAdd(
        HIKARI_CLUSTER_CULL_COUNTER_INPUT_COUNT,
        1);

    if (instance.clusterGeometryMetadataSrvDescriptorIndex < gClusterCullClusterSrvPoolBegin)
    {
        return;
    }

    uint clusterMetadataPoolIndex =
        instance.clusterGeometryMetadataSrvDescriptorIndex - gClusterCullClusterSrvPoolBegin;
    if (clusterMetadataPoolIndex >= gClusterCullClusterSrvPoolCount)
    {
        return;
    }

    ByteAddressBuffer metadata =
        gClusterGeometryPool[NonUniformResourceIndex(clusterMetadataPoolIndex)];
    HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(metadata);
    if (!HikariIsValidClusterGeometryHeader(header) ||
        instance.clusterRangeIndex >= header.clusterCount ||
        instance.clusterSurfaceIndex >= header.surfaceCount)
    {
        return;
    }

    HikariClusterGeometrySurface surface =
        HikariLoadClusterGeometrySurface(metadata, header, instance.clusterSurfaceIndex);
    if (surface.clusterCount == 0u ||
        surface.indexCount == 0u ||
        surface.firstSection >= header.surfaceSectionCount ||
        surface.sectionCount == 0u)
    {
        return;
    }

    uint sectionBegin = surface.firstSection;
    uint sectionEnd = min(sectionBegin + surface.sectionCount, header.surfaceSectionCount);
    for (uint sectionTableIndex = sectionBegin; sectionTableIndex < sectionEnd; ++sectionTableIndex)
    {
        HikariClusterGeometrySurfaceSection section =
            HikariLoadClusterGeometrySurfaceSection(metadata, header, sectionTableIndex);
        if (section.surfaceIndex != instance.clusterSurfaceIndex ||
            section.clusterCount == 0u ||
            section.indexCount == 0u ||
            section.firstLodRange >= header.surfaceLodRangeCount ||
            section.lodRangeCount == 0u)
        {
            continue;
        }

        float4 sectionWorldSphere = HikariClusterCullBuildWorldSphere(
            instance.clusterWorld,
            float4(0.0f, 0.0f, 0.0f, 0.0f),
            section.boundsMin,
            section.boundsMax);
        const bool vertexDeformed =
            (instance.flags & (
                HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED |
                HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX)) != 0u;
        if (!vertexDeformed && !HikariClusterCullSphereVisible(sectionWorldSphere))
        {
            if (gClusterCullEnableDebugCounters != 0u)
            {
                gClusterCullCounters.InterlockedAdd(
                    HIKARI_CLUSTER_CULL_COUNTER_INPUT_FRUSTUM_CULLED_COUNT,
                    1);
            }
            continue;
        }

        HikariClusterGeometrySurfaceLodRange selectedRange;
        if (!HikariClusterCullSelectSectionLodRange(
                metadata,
                header,
                instance,
                section,
                sectionWorldSphere,
                selectedRange))
        {
            continue;
        }

        ClusterCullInput input =
            HikariClusterCullBuildInput(
                surfaceGpuSceneIndex,
                instance,
                header,
                selectedRange,
                sectionWorldSphere);
        if (input.clusterIndexCount == 0u ||
            input.firstCluster >= header.clusterCount)
        {
            continue;
        }

        uint inputEndCluster = min(input.firstCluster + input.clusterCount, header.clusterCount);
        uint firstCluster = input.firstCluster;
        uint endCluster = inputEndCluster;
        if (firstCluster >= endCluster)
        {
            continue;
        }

        uint inputPageEnd = min(input.firstPage + input.pageCount, header.pageCount);
        uint firstPage = input.firstPage;
        uint endPage = inputPageEnd;
        if (firstPage >= endPage)
        {
            continue;
        }

        HikariClusterCullRecordSelectedLod(selectedRange.lodIndex);
        HikariClusterCullEmitPageTasks(
            input,
            firstCluster,
            endCluster,
            firstPage,
            endPage);
    }
}

