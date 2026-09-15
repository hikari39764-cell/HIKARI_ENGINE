#include "HIKARI_ClusterCullCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

// CullPageTasksCS は 1 lane = 1 page task。group 数が X 次元上限を超える
// 場合は Y 次元へ折り返す (CS 側は同じ幅で taskIndex を復元する)。
[numthreads(1, 1, 1)]
void FinalizePageTaskDispatchCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint pageTaskCount =
        gClusterCullCounters.Load(HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT);
    uint pageTaskOverflowCount = gClusterCullCounters.Load(
        HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT);
    if (pageTaskOverflowCount != 0u)
    {
        gClusterCullDispatchArguments[0] = uint3(0u, 1u, 1u);
        return;
    }
    uint clampedTaskCount = min(pageTaskCount, gClusterCullPageTaskCapacity);
    if (clampedTaskCount == 0u)
    {
        gClusterCullDispatchArguments[0] = uint3(0u, 1u, 1u);
        return;
    }

    uint groups = (clampedTaskCount + HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE - 1u) / HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE;
    uint groupCountX = min(groups, HIKARI_CLUSTER_CULL_PAGE_TASK_DISPATCH_MAX_X);
    uint groupCountY = (groups + groupCountX - 1u) / groupCountX;
    gClusterCullDispatchArguments[0] = uint3(groupCountX, groupCountY, 1u);
}
