struct GpuTraditionalCommandArgument
{
    uint2 vertexBufferLocation;
    uint vertexBufferSizeInBytes;
    uint vertexBufferStrideInBytes;

    uint2 indexBufferLocation;
    uint indexBufferSizeInBytes;
    uint indexBufferFormat;

    uint4 rootConstants;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
    uint reserved0;
};

struct GpuTraditionalSkinnedCommandArgument
{
    uint2 vertexBufferLocation;
    uint vertexBufferSizeInBytes;
    uint vertexBufferStrideInBytes;

    uint2 indexBufferLocation;
    uint indexBufferSizeInBytes;
    uint indexBufferFormat;

    uint4 rootConstants;

    uint2 jointPalette;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
    uint reserved0;
};

struct GpuTraditionalCommandPayload
{
    uint2 vertexBufferLocation;
    uint vertexBufferSizeInBytes;
    uint vertexBufferStrideInBytes;

    uint2 indexBufferLocation;
    uint indexBufferSizeInBytes;
    uint indexBufferFormat;

    uint2 jointPalette;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
    uint flags;
};

struct GpuTraditionalCommandSeed
{
    float4 boundsCenterRadius;
    uint absoluteGpuSceneInstanceIndex;
    uint flags;
    uint passIndex;
    uint bucketIndex;
    uint payloadIndex;
    uint localGpuSceneInstanceIndex;
    uint reserved0;
    uint reserved1;
};

cbuffer GpuTraditionalCommandStreamCullingCB : register(b0)
{
    float4x4 gGpuTraditionalCommandStreamViewProj;
    uint gGpuTraditionalCommandStreamInputCount;
    uint gGpuTraditionalCommandStreamOutputCapacity;
    uint gGpuTraditionalCommandStreamEnableFrustumCull;
    uint gGpuTraditionalCommandStreamPayloadCount;
};

StructuredBuffer<GpuTraditionalCommandSeed> gGpuTraditionalCommandStreamSeeds : register(t0);
StructuredBuffer<GpuTraditionalCommandPayload> gGpuTraditionalCommandStreamPayloads : register(t1);
RWStructuredBuffer<GpuTraditionalCommandArgument> gGpuTraditionalCommandStreamArguments : register(u0);
RWStructuredBuffer<GpuTraditionalSkinnedCommandArgument> gGpuTraditionalSkinnedCommandArguments : register(u1);
RWByteAddressBuffer gGpuTraditionalCommandStreamCounters : register(u2);

static const uint HIKARI_SURFACE_INDIRECT_COUNTER_DRAW_COUNT = 0u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_VISIBLE_COUNT = 4u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT = 8u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_OVERFLOW_COUNT = 12u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_SKINNED_DRAW_COUNT = 16u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_SKINNED_VISIBLE_COUNT = 20u;
static const uint HIKARI_SURFACE_INDIRECT_COUNTER_STRIDE_BYTES = 32u;
static const uint HIKARI_SURFACE_INDIRECT_BUCKET_COUNT = 32u;
static const uint HIKARI_SURFACE_INDIRECT_FLAG_SKINNED = 1u << 1;

float4 HikariGpuTraditionalCommandStreamViewProjRow0()
{
    return float4(
        gGpuTraditionalCommandStreamViewProj._11,
        gGpuTraditionalCommandStreamViewProj._12,
        gGpuTraditionalCommandStreamViewProj._13,
        gGpuTraditionalCommandStreamViewProj._14);
}

float4 HikariGpuTraditionalCommandStreamViewProjRow1()
{
    return float4(
        gGpuTraditionalCommandStreamViewProj._21,
        gGpuTraditionalCommandStreamViewProj._22,
        gGpuTraditionalCommandStreamViewProj._23,
        gGpuTraditionalCommandStreamViewProj._24);
}

float4 HikariGpuTraditionalCommandStreamViewProjRow2()
{
    return float4(
        gGpuTraditionalCommandStreamViewProj._31,
        gGpuTraditionalCommandStreamViewProj._32,
        gGpuTraditionalCommandStreamViewProj._33,
        gGpuTraditionalCommandStreamViewProj._34);
}

float4 HikariGpuTraditionalCommandStreamViewProjRow3()
{
    return float4(
        gGpuTraditionalCommandStreamViewProj._41,
        gGpuTraditionalCommandStreamViewProj._42,
        gGpuTraditionalCommandStreamViewProj._43,
        gGpuTraditionalCommandStreamViewProj._44);
}

bool HikariGpuTraditionalCommandStreamPlaneVisible(float4 plane, float3 center, float radius)
{
    const float planeLength = length(plane.xyz);
    if (planeLength <= 0.000001f)
    {
        return true;
    }
    return dot(plane.xyz, center) + plane.w >= -radius * planeLength;
}

bool HikariGpuTraditionalCommandStreamFrustumVisible(float4 boundsCenterRadius)
{
    const float radius = boundsCenterRadius.w;
    if (radius < 0.0f)
    {
        return true;
    }

    const float3 center = boundsCenterRadius.xyz;
    const float4 row0 = HikariGpuTraditionalCommandStreamViewProjRow0();
    const float4 row1 = HikariGpuTraditionalCommandStreamViewProjRow1();
    const float4 row2 = HikariGpuTraditionalCommandStreamViewProjRow2();
    const float4 row3 = HikariGpuTraditionalCommandStreamViewProjRow3();

    return
        HikariGpuTraditionalCommandStreamPlaneVisible(row3 + row0, center, radius) &&
        HikariGpuTraditionalCommandStreamPlaneVisible(row3 - row0, center, radius) &&
        HikariGpuTraditionalCommandStreamPlaneVisible(row3 + row1, center, radius) &&
        HikariGpuTraditionalCommandStreamPlaneVisible(row3 - row1, center, radius) &&
        HikariGpuTraditionalCommandStreamPlaneVisible(row2, center, radius) &&
        HikariGpuTraditionalCommandStreamPlaneVisible(row3 - row2, center, radius);
}

[numthreads(64, 1, 1)]
void CompactGpuTraditionalCommandStreamCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint seedIndex = dispatchThreadId.x;
    if (seedIndex >= gGpuTraditionalCommandStreamInputCount)
    {
        return;
    }

    const GpuTraditionalCommandSeed seed = gGpuTraditionalCommandStreamSeeds[seedIndex];
    const uint bucketIndex =
        min(seed.bucketIndex, HIKARI_SURFACE_INDIRECT_BUCKET_COUNT - 1u);
    const uint passBucketIndex =
        seed.passIndex * HIKARI_SURFACE_INDIRECT_BUCKET_COUNT + bucketIndex;
    const uint passCounterBase =
        passBucketIndex * HIKARI_SURFACE_INDIRECT_COUNTER_STRIDE_BYTES;
    const uint passOutputBase =
        passBucketIndex * gGpuTraditionalCommandStreamOutputCapacity;
    if (seed.payloadIndex >= gGpuTraditionalCommandStreamPayloadCount)
    {
        uint ignoredPayload = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT,
            1u,
            ignoredPayload);
        return;
    }
    const GpuTraditionalCommandPayload payload =
        gGpuTraditionalCommandStreamPayloads[seed.payloadIndex];
    const bool skinned = (seed.flags & HIKARI_SURFACE_INDIRECT_FLAG_SKINNED) != 0u;
    const uint2 vertexLocation = payload.vertexBufferLocation;
    const uint2 indexLocation = payload.indexBufferLocation;
    const uint indexCount = payload.indexCountPerInstance;
    const uint instanceCount = payload.instanceCount;
    const bool hasVertexBuffer =
        vertexLocation.x != 0u ||
        vertexLocation.y != 0u;
    const bool hasIndexBuffer =
        indexLocation.x != 0u ||
        indexLocation.y != 0u;
    const bool hasJointPalette =
        !skinned ||
        payload.jointPalette.x != 0u ||
        payload.jointPalette.y != 0u;
    if (indexCount == 0u ||
        instanceCount == 0u ||
        !hasVertexBuffer ||
        !hasIndexBuffer ||
        !hasJointPalette)
    {
        uint ignored = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT,
            1u,
            ignored);
        return;
    }

    const bool visible =
        gGpuTraditionalCommandStreamEnableFrustumCull == 0u ||
        HikariGpuTraditionalCommandStreamFrustumVisible(seed.boundsCenterRadius);
    if (!visible)
    {
        uint ignored = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_CULLED_COUNT,
            1u,
            ignored);
        return;
    }

    uint ignoredVisible = 0u;
    gGpuTraditionalCommandStreamCounters.InterlockedAdd(
        passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_VISIBLE_COUNT,
        1u,
        ignoredVisible);

    if (skinned)
    {
        uint visibleSkinnedIndex = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_SKINNED_DRAW_COUNT,
            1u,
            visibleSkinnedIndex);
        uint ignoredSkinnedVisible = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_SKINNED_VISIBLE_COUNT,
            1u,
            ignoredSkinnedVisible);
        if (visibleSkinnedIndex >= gGpuTraditionalCommandStreamOutputCapacity)
        {
            uint ignoredSkinnedDrawRollback = 0u;
            gGpuTraditionalCommandStreamCounters.InterlockedAdd(
                passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_SKINNED_DRAW_COUNT,
                0xffffffffu,
                ignoredSkinnedDrawRollback);
            uint ignoredOverflow = 0u;
            gGpuTraditionalCommandStreamCounters.InterlockedAdd(
                passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_OVERFLOW_COUNT,
                1u,
                ignoredOverflow);
            return;
        }

        GpuTraditionalSkinnedCommandArgument argument;
        argument.vertexBufferLocation = payload.vertexBufferLocation;
        argument.vertexBufferSizeInBytes = payload.vertexBufferSizeInBytes;
        argument.vertexBufferStrideInBytes = payload.vertexBufferStrideInBytes;
        argument.indexBufferLocation = payload.indexBufferLocation;
        argument.indexBufferSizeInBytes = payload.indexBufferSizeInBytes;
        argument.indexBufferFormat = payload.indexBufferFormat;
        argument.jointPalette = payload.jointPalette;
        argument.rootConstants.x = seed.absoluteGpuSceneInstanceIndex;
        argument.rootConstants.y = 1u;
        argument.rootConstants.z = 0u;
        argument.rootConstants.w = 0u;
        argument.indexCountPerInstance = payload.indexCountPerInstance;
        argument.instanceCount = 1u;
        argument.startIndexLocation = payload.startIndexLocation;
        argument.baseVertexLocation = payload.baseVertexLocation;
        argument.startInstanceLocation = 0u;
        argument.reserved0 = 0u;
        gGpuTraditionalSkinnedCommandArguments[passOutputBase + visibleSkinnedIndex] = argument;
        return;
    }

    uint visibleIndex = 0u;
    gGpuTraditionalCommandStreamCounters.InterlockedAdd(
        passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_DRAW_COUNT,
        1u,
        visibleIndex);

    if (visibleIndex >= gGpuTraditionalCommandStreamOutputCapacity)
    {
        uint ignoredDrawRollback = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_DRAW_COUNT,
            0xffffffffu,
            ignoredDrawRollback);
        uint ignoredOverflow = 0u;
        gGpuTraditionalCommandStreamCounters.InterlockedAdd(
            passCounterBase + HIKARI_SURFACE_INDIRECT_COUNTER_OVERFLOW_COUNT,
            1u,
            ignoredOverflow);
        return;
    }

    GpuTraditionalCommandArgument argument;
    argument.vertexBufferLocation = payload.vertexBufferLocation;
    argument.vertexBufferSizeInBytes = payload.vertexBufferSizeInBytes;
    argument.vertexBufferStrideInBytes = payload.vertexBufferStrideInBytes;
    argument.indexBufferLocation = payload.indexBufferLocation;
    argument.indexBufferSizeInBytes = payload.indexBufferSizeInBytes;
    argument.indexBufferFormat = payload.indexBufferFormat;
    argument.rootConstants.x = seed.absoluteGpuSceneInstanceIndex;
    argument.rootConstants.y = 1u;
    argument.rootConstants.z = 0u;
    argument.rootConstants.w = 0u;
    argument.indexCountPerInstance = payload.indexCountPerInstance;
    argument.instanceCount = 1u;
    argument.startIndexLocation = payload.startIndexLocation;
    argument.baseVertexLocation = payload.baseVertexLocation;
    argument.startInstanceLocation = 0u;
    argument.reserved0 = 0u;
    gGpuTraditionalCommandStreamArguments[passOutputBase + visibleIndex] = argument;
}
