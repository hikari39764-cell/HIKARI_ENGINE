#include "HIKARI_ClusterCullCommon.hlsli"

// CullPageTasksCS は 1 group = 1 page task。task 数が X 次元上限を超える
// 場合は Y 次元へ折り返す (CS 側は同じ幅で taskIndex を復元する)。
[numthreads(1, 1, 1)]
void FinalizePageTaskDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint pageTaskCount =
        gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    uint clampedTaskCount = min(pageTaskCount, gClusterCullPageTaskCapacity);
    if (clampedTaskCount == 0u)
    {
        gClusterCullDispatchArguments[0] = uint3(0u, 1u, 1u);
        return;
    }

    uint groupCountX =
        min(clampedTaskCount, HIKARI_CLUSTER_CULL_PAGE_TASK_DISPATCH_MAX_X);
    uint groupCountY = (clampedTaskCount + groupCountX - 1u) / groupCountX;
    gClusterCullDispatchArguments[0] = uint3(groupCountX, groupCountY, 1u);
}
