#include "Include/HIKARI_SurfaceGpuScene.hlsli" // 大量配置の記録（HIKARI_MassGpuScene.hlsli）もここから

// 光線追跡の加速構造の最上位へ、大量配置の個体の instance 記述（D3D12_RAYTRACING_INSTANCE_DESC、64 byte）を書く。
// 個体の記録は描画と同じ常駐 buffer（gMassRenderProxyBuffer）から読み、変換も描画と同じ HikariBuildMassMatrices で作る
// （CPU は記述を upload しない）。種類ごとの並びと形の塊（BLAS の番地と hit 記録の先頭）は CPU が組み立て時に一度書く。

struct MassInstanceStore
{
    uint firstRecord;   // 個体の記録の先頭（大量配置の記録の並びの中）
    uint recordCount;
    uint firstInstance; // 最上位の instance 記述の中の先頭
    uint chunkCount;    // 1 個体あたりの instance 数（形の塊の数）
    uint firstChunk;    // gMassInstanceChunks の中の先頭
    uint3 padding;
};

struct MassInstanceChunk
{
    uint2 bottomLevel; // BLAS の GPU 番地（下位, 上位）
    uint hitBase;      // InstanceID（hit 記録の先頭）
    uint padding;
};

cbuffer RayTracingMassInstances : register(b0)
{
    uint gRecordCount;
    uint gStoreCount;
    uint2 gJobPadding;
};

StructuredBuffer<MassInstanceStore> gMassInstanceStores : register(t1);
StructuredBuffer<MassInstanceChunk> gMassInstanceChunks : register(t2);
RWByteAddressBuffer gTopLevelInstances : register(u0);

static const uint kInstanceDescBytes = 64u;
static const uint kTriangleCullDisable = 0x1u;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint recordIndex = id.x;
    if (recordIndex >= gRecordCount)
        return;
    const HikariMassRenderProxyGpuRecord record = gMassRenderProxyBuffer[recordIndex];
    if (record.sourceRecordIndex >= gStoreCount)
        return;
    const MassInstanceStore store = gMassInstanceStores[record.sourceRecordIndex];
    if (recordIndex < store.firstRecord || recordIndex - store.firstRecord >= store.recordCount)
        return;
    float4x4 world;
    float4x4 normalMatrix;
    HikariBuildMassMatrices(record, world, normalMatrix);
    // 値が有限でない個体は光線から隠す（CPU の組み立てと同じ）。
    const bool finite = all(isfinite(world[0])) && all(isfinite(world[1])) && all(isfinite(world[2]));
    if (!finite)
        world = (float4x4)0;
    // kRuntimeRayTracingSceneMask（代理だけの光線には当たらない）。
    const uint mask = finite ? 0xfeu : 0u;
    for (uint chunk = 0u; chunk < store.chunkCount; ++chunk)
    {
        const MassInstanceChunk shape = gMassInstanceChunks[store.firstChunk + chunk];
        const uint offset =
            (store.firstInstance + (recordIndex - store.firstRecord) * store.chunkCount + chunk) * kInstanceDescBytes;
        gTopLevelInstances.Store4(offset, asuint(world[0]));
        gTopLevelInstances.Store4(offset + 16u, asuint(world[1]));
        gTopLevelInstances.Store4(offset + 32u, asuint(world[2]));
        gTopLevelInstances.Store4(offset + 48u,
            uint4((shape.hitBase & 0x00ffffffu) | (mask << 24u), kTriangleCullDisable << 24u,
                shape.bottomLevel.x, shape.bottomLevel.y));
    }
}
