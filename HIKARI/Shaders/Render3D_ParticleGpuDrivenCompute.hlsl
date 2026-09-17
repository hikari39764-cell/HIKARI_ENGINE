#include "Render3D_ParticleSimulation.hlsl"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

RWStructuredBuffer<uint> gAliveParticleIndices : register(u1);
RWStructuredBuffer<uint> gParticleDrawCounters : register(u2);
struct ParticleDrawArguments { uint3 mesh; uint reserved; uint4 raster; };
RWStructuredBuffer<ParticleDrawArguments> gParticleDrawArguments : register(u3);

cbuffer CompactConstants : register(b1)
{
    uint gCompactParticleOffset;
    uint gCompactParticleCapacity;
    uint gCompactDrawIndex;
    uint gCompactAliveIndexBase;
    uint gCompactVertexCount;
};

[numthreads(1, 1, 1)]
void ResetDrawCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }
    gParticleDrawCounters[gCompactDrawIndex] = 0u;
    gParticleDrawArguments[gCompactDrawIndex] = (ParticleDrawArguments)0;
}

[numthreads(64, 1, 1)]
void CompactDrawCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint localIndex = dispatchThreadId.x;
    if (localIndex >= gCompactParticleCapacity)
    {
        return;
    }
    const uint particleIndex = gCompactParticleOffset + localIndex;
    if (gParticles[particleIndex].velocityLifetime.w <= 0.0f)
    {
        return;
    }
    uint compactedIndex = 0u;
    InterlockedAdd(
        gParticleDrawCounters[gCompactDrawIndex],
        1u,
        compactedIndex);
    gAliveParticleIndices[gCompactAliveIndexBase + compactedIndex] =
        particleIndex;
}

[numthreads(1, 1, 1)]
void BuildDrawArgsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }
    const uint aliveCount = gParticleDrawCounters[gCompactDrawIndex];
    ParticleDrawArguments args;
    args.mesh = uint3((aliveCount+31u)/32u,1u,1u);
    args.reserved = 0u;
    args.raster = uint4(gCompactVertexCount,aliveCount,0u,0u);
    gParticleDrawArguments[gCompactDrawIndex] = args;
}
