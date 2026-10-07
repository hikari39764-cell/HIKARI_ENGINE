#ifndef HIKARI_POST_SURFACE_INPUTS_HLSLI
#define HIKARI_POST_SURFACE_INPUTS_HLSLI

#include "../Features/HIKARI_StylizedFeatureMask.hlsli"

// Declare inputs:["SurfaceFeatureMask"] in the .hposteffect.
// Current-view opaque/alpha-tested depth winner, R32G32_UINT at render resolution.
// Transparent blends do not write classification; the opaque surface behind
// them remains visible to this input. No temporal filtering or dilation.
Texture2D<uint2> HikariPostSurfaceFeatureMask : register(t2);

uint2 HikariPostReadSurfaceFeatureMask(float2 screenUv)
{
    uint width, height;
    HikariPostSurfaceFeatureMask.GetDimensions(width, height);
    if (width == 0u || height == 0u)
        return uint2(0u, 0u);
    uint2 pixel = min(uint2(saturate(screenUv) * float2(width, height)), uint2(width - 1u, height - 1u));
    return HikariPostSurfaceFeatureMask.Load(int3(pixel, 0));
}

// Automatic per-object group hashes are never project classification bits.
uint HikariPostReadGroupMask(float2 screenUv)
{
    uint group = HikariPostReadSurfaceFeatureMask(screenUv).y;
    return (group & HIKARI_STYLIZED_AUTO_GROUP) != 0u ? 0u : group;
}

bool HikariPostHasGroup(float2 screenUv, uint groups)
{
    return (HikariPostReadGroupMask(screenUv) & groups) != 0u;
}
#endif
