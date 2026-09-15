#include "HIKARI_ClusterCullCommon.hlsli"

#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

[numthreads(1, 1, 1)]
void FinalizeMeshletDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    bool overflowed =
        gClusterCullCounters.Load(
            HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT) != 0u ||
        gClusterCullCounters.Load(
            HIKARI_CLUSTER_CULL_COUNTER_OVERFLOW_COUNT) != 0u ||
        gClusterCullCounters.Load(
            HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_OVERFLOW_COUNT) != 0u ||
        gClusterCullCounters.Load(
            HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_DRAW_OVERFLOW_COUNT) != 0u;
    [unroll]
    for (uint passKind = 0u; passKind < HIKARI_CLUSTER_CULL_PASS_COUNT; ++passKind)
    {
        [unroll]
        for (uint bucket = 0u; bucket < HIKARI_CLUSTER_DRAW_BUCKET_COUNT; ++bucket)
        {
            uint drawCounterOffset =
                HikariClusterCullPassDrawCounterOffset(passKind, bucket);
            uint drawCount = gClusterCullCounters.Load(drawCounterOffset);
            uint clampedDrawCount = min(
                drawCount,
                gClusterCullCandidateCommandBucketCapacity);
            uint bucketBase = HikariClusterCullPassBucketBaseIndex(passKind, bucket);

            ClusterCullMeshletDispatchArgument meshletArgument;
            meshletArgument.rootConstants = uint4(
                bucketBase,
                passKind,
                bucket,
                1u);
            meshletArgument.threadGroupCountX = overflowed
                ? 0u
                : clampedDrawCount;
            meshletArgument.threadGroupCountY = 1u;
            meshletArgument.threadGroupCountZ = 1u;
            meshletArgument.reserved0 = 0u;
            // range の base は root constants、command 自体は連続した 10 slots。
            gClusterCullMeshletDispatchArguments[passKind * HIKARI_CLUSTER_DRAW_BUCKET_COUNT + bucket] = meshletArgument;
        }
    }
}
