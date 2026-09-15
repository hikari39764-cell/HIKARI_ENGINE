#include "Include/Depth/HIKARI_DepthPyramidInterface.hlsli"

float LoadSourceDepth(uint2 pixel)
{
    pixel = min(pixel, gSourceSize - 1u);
    return gSourceDepth.Load(int3(pixel, 0));
}

[numthreads(
    HIKARI_DEPTH_PYRAMID_THREAD_GROUP_SIZE,
    HIKARI_DEPTH_PYRAMID_THREAD_GROUP_SIZE,
    1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 dst = dispatchThreadId.xy;
    if (any(dst >= gDestSize))
    {
        return;
    }

    // 通常の偶数 mip は整数除算も追加 sample も不要。
    [branch] if (all(gSourceSize == gDestSize * 2u))
    {
        const uint2 src = dst * 2u;
        gDestDepthPyramid[dst] = max(max(LoadSourceDepth(src), LoadSourceDepth(src + uint2(1u, 0u))),
            max(LoadSourceDepth(src + uint2(0u, 1u)), LoadSourceDepth(src + uint2(1u, 1u))));
        return;
    }
    // NPOT mip の端も含める。単純な dst*2 では奇数 source の最終列/行が落ちる。
    const uint2 begin = dst * gSourceSize / gDestSize;
    const uint2 end = ((dst + 1u) * gSourceSize + gDestSize - 1u) / gDestSize;
    float farthest = 0.0f;
    [unroll] for (uint y = 0u; y < 3u; ++y)
    [unroll] for (uint x = 0u; x < 3u; ++x)
        if (begin.x + x < end.x && begin.y + y < end.y)
            farthest = max(farthest, LoadSourceDepth(begin + uint2(x, y)));
    gDestDepthPyramid[dst] = farthest;
}
