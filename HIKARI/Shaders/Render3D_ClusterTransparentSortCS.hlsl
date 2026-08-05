#include "HIKARI_ClusterCullCommon.hlsli"

static const uint HIKARI_TRANSPARENT_SORT_MAX_RANGES = 4096u;

float HikariTransparentSortDistanceSq(ClusterCullVisibleRange range)
{
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(range.gpuSceneInstanceIndex);
    const float3 toCamera =
        instance.boundsCenterRadius.xyz - gClusterCullCameraPosition.xyz;
    return dot(toCamera, toCamera);
}

bool HikariTransparentComesBefore(
    ClusterCullVisibleRange left,
    float leftDistanceSq,
    ClusterCullVisibleRange right,
    float rightDistanceSq)
{
    const float depthTolerance =
        max(max(leftDistanceSq, rightDistanceSq), 1.0f) * 1.0e-6f;
    if (abs(leftDistanceSq - rightDistanceSq) > depthTolerance)
    {
        return leftDistanceSq > rightDistanceSq;
    }
    const HikariSurfaceGpuSceneInstance leftInstance =
        HikariGetSurfaceGpuSceneInstanceAt(left.gpuSceneInstanceIndex);
    const HikariSurfaceGpuSceneInstance rightInstance =
        HikariGetSurfaceGpuSceneInstanceAt(right.gpuSceneInstanceIndex);
    if (leftInstance.sourceRecordIndex != rightInstance.sourceRecordIndex)
    {
        return leftInstance.sourceRecordIndex < rightInstance.sourceRecordIndex;
    }
    if (left.firstCluster != right.firstCluster)
    {
        return left.firstCluster < right.firstCluster;
    }
    return left.sectionIndex < right.sectionIndex;
}

[numthreads(1, 1, 1)]
void SortTransparentRangesCS(uint3 groupId : SV_GroupID)
{
    if (gClusterCullPassKind !=
            HIKARI_CLUSTER_CULL_PASS_FORWARD_TRANSPARENT ||
        groupId.x >= HIKARI_CLUSTER_DRAW_BUCKET_COUNT)
    {
        return;
    }

    const uint bucket = groupId.x;
    const uint counterOffset = HikariClusterCullPassDrawCounterOffset(
        gClusterCullPassKind,
        bucket);
    const uint count = min(
        gClusterCullCounters.Load(counterOffset),
        gClusterCullCandidateCommandBucketCapacity);
    if (count < 2u || count > HIKARI_TRANSPARENT_SORT_MAX_RANGES)
    {
        return;
    }
    const uint baseIndex = HikariClusterCullPassBucketBaseIndex(
        gClusterCullPassKind,
        bucket);

    // Shell sort is exact, deterministic, in-place and bounded to the normal
    // transparent-range budget. It avoids a second 144-byte-per-range scratch
    // allocation while preserving the Mesh Shader visible-range contract.
    for (uint gap = count / 2u; gap > 0u; gap /= 2u)
    {
        for (uint index = gap; index < count; ++index)
        {
            const ClusterCullVisibleRange candidate =
                gClusterCullVisibleRanges[baseIndex + index];
            const float candidateDistanceSq =
                HikariTransparentSortDistanceSq(candidate);
            uint insertion = index;
            while (insertion >= gap)
            {
                const uint previousIndex = insertion - gap;
                const ClusterCullVisibleRange previous =
                    gClusterCullVisibleRanges[baseIndex + previousIndex];
                const float previousDistanceSq =
                    HikariTransparentSortDistanceSq(previous);
                if (!HikariTransparentComesBefore(
                        candidate,
                        candidateDistanceSq,
                        previous,
                        previousDistanceSq))
                {
                    break;
                }
                gClusterCullVisibleRanges[baseIndex + insertion] = previous;
                insertion = previousIndex;
            }
            gClusterCullVisibleRanges[baseIndex + insertion] = candidate;
        }
    }
}
