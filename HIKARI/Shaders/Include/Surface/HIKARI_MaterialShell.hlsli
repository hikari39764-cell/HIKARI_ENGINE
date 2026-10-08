#ifndef HIKARI_MATERIAL_SHELL_INCLUDED
#define HIKARI_MATERIAL_SHELL_INCLUDED

// 全 view / pass が同じ投影規則を使用する。layer は固定 8 段階の部分集合。
float HikariShellLodAmount(float4 settings, float4 bounds, float4x4 viewProj, float screenHeight)
{
    if (settings.x < 2.0f || settings.y <= 0.0f || screenHeight <= 0.0f) return 0.0f;
    float4 centerClip = mul(viewProj, float4(bounds.xyz, 1.0f));
    float pixelsPerWorld = 0.5f * screenHeight * length(viewProj[1].xyz) /
        max(abs(centerClip.w), 1.0e-4f);
    float diameterPixels = 2.0f * abs(bounds.w) * pixelsPerWorld;
    float footprint = saturate((diameterPixels - settings.z) / max(settings.w - settings.z, 1.0f));
    // 画素未満の厚さに geometry を重ねない。
    float thickness = saturate((settings.y * pixelsPerWorld - 0.5f) / 1.5f);
    return footprint * thickness;
}

uint HikariShellLayerCount(float4 settings, float amount)
{
    uint count = amount > 0.75f ? 8u : amount > 0.35f ? 4u : amount > 0.0f ? 2u : 0u;
    return min(count, (uint)settings.x);
}

float4 HikariShellLayer(uint ordinal, uint layerCount, float amount)
{
    if (ordinal == 0u || layerCount == 0u) return float4(0, 1, 0, 1);
    uint id = layerCount == 2u ? ordinal * 4u - 2u : layerCount == 4u ? ordinal * 2u : ordinal;
    float threshold = (id & 1u) != 0u ? 0.75f : (id % 4u) == 0u ? 0.35f : 0.0f;
    float fade = smoothstep(threshold, threshold + 0.15f, amount);
    return float4(float(id) * 0.125f, 0.0f, float(id), fade);
}

void HikariApplyShellFade(float4 layer, float2 pixelPosition)
{
    if (layer.y < 0.5f)
    {
        // 決定的な coverage。main と aux は同じ pixel で一致し、時間乱数を持たない。
        uint2 p = (uint2)pixelPosition;
        uint hash = p.x * 1597334677u ^ p.y * 3812015801u;
        hash ^= hash >> 16u;
        clip(layer.w - (float(hash & 255u) + 0.5f) / 256.0f);
    }
}

// 殻の押し出しの揺らぎ（筆の圧の濃淡）。局所座標の粗い値ノイズで厚みを 1 ± HIKARI_SHELL_PUSH_VARIATION 倍する。
// 格子は物体の半径で正規化し（半径 ≈ 4 格子）、全 view / pass が同じ式を使う。
static const float HIKARI_SHELL_PUSH_VARIATION = 0.45f;

float HikariShellPushHash(float3 cell)
{
    const int3 i = (int3)cell;
    uint hash = (uint)i.x * 73856093u ^ (uint)i.y * 19349663u ^ (uint)i.z * 83492791u;
    hash ^= hash >> 13u;
    hash *= 0x5bd1e995u;
    hash ^= hash >> 15u;
    return float(hash & 0xffffffu) / 16777216.0f;
}

float HikariShellPush(float3 localPosition, float boundsRadius)
{
    const float3 p = localPosition * (4.0f / max(boundsRadius, 1.0e-3f));
    const float3 cell = floor(p);
    const float3 t = p - cell;
    const float3 w = t * t * (3.0f - 2.0f * t);
    const float x00 = lerp(HikariShellPushHash(cell), HikariShellPushHash(cell + float3(1, 0, 0)), w.x);
    const float x10 = lerp(HikariShellPushHash(cell + float3(0, 1, 0)), HikariShellPushHash(cell + float3(1, 1, 0)), w.x);
    const float x01 = lerp(HikariShellPushHash(cell + float3(0, 0, 1)), HikariShellPushHash(cell + float3(1, 0, 1)), w.x);
    const float x11 = lerp(HikariShellPushHash(cell + float3(0, 1, 1)), HikariShellPushHash(cell + float3(1, 1, 1)), w.x);
    const float noise = lerp(lerp(x00, x10, w.y), lerp(x01, x11, w.y), w.z);
    return 1.0f + HIKARI_SHELL_PUSH_VARIATION * (noise * 2.0f - 1.0f);
}

#endif
