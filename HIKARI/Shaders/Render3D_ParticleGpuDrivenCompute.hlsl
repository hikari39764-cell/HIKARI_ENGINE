#include "Render3D_ParticleSimulation.hlsl"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

RWStructuredBuffer<uint> gAliveParticleIndices : register(u1);
RWStructuredBuffer<uint> gParticleDrawCounters : register(u2);
RWStructuredBuffer<uint3> gParticleDrawArguments : register(u3);

cbuffer CompactConstants : register(b1)
{
    uint gCompactParticleOffset;
    uint gCompactParticleCapacity;
    uint gCompactDrawIndex;
    uint gCompactAliveIndexBase;
};

[numthreads(1, 1, 1)]
void ResetDrawCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }
    gParticleDrawCounters[gCompactDrawIndex] = 0u;
    gParticleDrawArguments[gCompactDrawIndex] = uint3(0u, 1u, 1u);
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
    gParticleDrawArguments[gCompactDrawIndex] = uint3(
        gParticleDrawCounters[gCompactDrawIndex],
        1u,
        1u);
}
