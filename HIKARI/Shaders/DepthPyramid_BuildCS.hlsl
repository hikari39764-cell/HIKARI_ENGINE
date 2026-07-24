cbuffer DepthPyramidBuildCB : register(b0)
{
    uint2 gSourceSize;
    uint2 gDestSize;
};

Texture2D<float> gSourceDepth : register(t0);
RWTexture2D<float> gDestDepthPyramid : register(u0);

float LoadSourceDepth(uint2 pixel)
{
    pixel = min(pixel, gSourceSize - 1u);
    return gSourceDepth.Load(int3(pixel, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 dst = dispatchThreadId.xy;
    if (any(dst >= gDestSize))
    {
        return;
    }

    const uint2 src = dst * 2u;
    const float d0 = LoadSourceDepth(src);
    const float d1 = LoadSourceDepth(src + uint2(1u, 0u));
    const float d2 = LoadSourceDepth(src + uint2(0u, 1u));
    const float d3 = LoadSourceDepth(src + uint2(1u, 1u));

    // Standard D3D less-depth: larger depth is farther. Store the farthest
    // covered depth so an occlusion test only passes when the whole sample is in front.
    gDestDepthPyramid[dst] = max(max(d0, d1), max(d2, d3));
}
