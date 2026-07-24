#ifndef HIKARI_MESHLET_AS_COMPACTION_INCLUDED
#define HIKARI_MESHLET_AS_COMPACTION_INCLUDED

groupshared uint gMeshletAsVisibleCount;
groupshared uint gMeshletAsWaveVisibleCounts[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];
groupshared uint gMeshletAsWaveVisibleOffsets[HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD];

void HikariMeshletAsResetCompaction()
{
    gMeshletAsVisibleCount = 0u;
}

// 全 lane が 1 回だけ呼び出す。Wave 内 prefix と wave 間 prefix の二段階で
// atomic を使わず、可視 meshlet を payload の連続領域へ圧縮する。
uint HikariMeshletAsCompactVisibleLane(bool laneVisible, uint groupIndex)
{
    const uint waveLaneCount = WaveGetLaneCount();
    const uint waveIndex = groupIndex / waveLaneCount;
    const uint waveLaneIndex = WaveGetLaneIndex();
    const uint wavePrefix = WavePrefixCountBits(laneVisible);
    const uint waveVisibleCount = WaveActiveCountBits(laneVisible);

    if (waveLaneIndex == 0u)
    {
        gMeshletAsWaveVisibleCounts[waveIndex] = waveVisibleCount;
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0u)
    {
        const uint waveCount =
            (HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD + waveLaneCount - 1u) /
            waveLaneCount;
        uint visibleCount = 0u;
        [loop]
        for (uint index = 0u; index < waveCount; ++index)
        {
            gMeshletAsWaveVisibleOffsets[index] = visibleCount;
            visibleCount += gMeshletAsWaveVisibleCounts[index];
        }
        gMeshletAsVisibleCount = min(
            visibleCount,
            HIKARI_MESHLET_AS_MAX_CLUSTER_PAYLOAD);
    }
    GroupMemoryBarrierWithGroupSync();

    return gMeshletAsWaveVisibleOffsets[waveIndex] + wavePrefix;
}

#endif
