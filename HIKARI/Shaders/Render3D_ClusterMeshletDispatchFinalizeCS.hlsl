#include "HIKARI_ClusterCullCommon.hlsli"

[numthreads(1, 1, 1)]
void FinalizeMeshletDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
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
            meshletArgument.threadGroupCountX = clampedDrawCount;
            meshletArgument.threadGroupCountY = 1u;
            meshletArgument.threadGroupCountZ = 1u;
            meshletArgument.reserved0 = 0u;
            gClusterCullMeshletDispatchArguments[bucketBase] = meshletArgument;
        }
    }
}
