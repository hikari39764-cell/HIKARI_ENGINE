#ifndef HIKARI_DEPTH_OCCLUSION_INCLUDED
#define HIKARI_DEPTH_OCCLUSION_INCLUDED

// Standard D3D less-depth / max hierarchy。CS と AS は同じ保守的矩形を使う。
struct HikariDepthOcclusionQuery
{
    float2 minUv;
    float2 maxUv;
    float nearestDepth;
    float maxExtentPixels;
    uint flags;
    uint rejectReason; // 0: valid, 1: invalid, 2: near plane, 3: offscreen
};

bool HikariBuildDepthOcclusionQuery(float4x4 viewProj, float4x4 world,
    float3 boundsMin, float3 boundsMax, uint2 size, out HikariDepthOcclusionQuery query)
{
    query = (HikariDepthOcclusionQuery)0;
    query.rejectReason = 1u;
    query.minUv = 1.0f.xx;
    query.maxUv = -1.0f.xx;
    query.nearestDepth = 1.0f;
    if (any(size == 0u) || !all(isfinite(boundsMin)) || !all(isfinite(boundsMax)) || any(boundsMax < boundsMin)) return false;
    float4x4 clipFromLocal = mul(viewProj, world);
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        float3 p = float3((corner & 1u) ? boundsMax.x : boundsMin.x,
            (corner & 2u) ? boundsMax.y : boundsMin.y,
            (corner & 4u) ? boundsMax.z : boundsMin.z);
        float4 clip = mul(clipFromLocal, float4(p, 1.0f));
        // near-plane を横切る物体は可視。球への置換で矩形を縮めない。
        if (!all(isfinite(clip))) return false;
        if (clip.w <= 0.0001f || clip.z <= 0.0f) { query.rejectReason = 2u; return false; }
        float3 ndc = clip.xyz / clip.w;
        float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        query.minUv = min(query.minUv, uv);
        query.maxUv = max(query.maxUv, uv);
        query.nearestDepth = min(query.nearestDepth, ndc.z);
    }
    if (any(query.maxUv <= 0.0f) || any(query.minUv >= 1.0f)) { query.rejectReason = 3u; return false; }
    // 量子化 / raster coverage の境界で誤剔除しないよう mip0 の 1 texel を広げる。
    query.minUv = saturate(query.minUv - 1.0f / float2(size));
    query.maxUv = saturate(query.maxUv + 1.0f / float2(size));
    float2 extent = (query.maxUv - query.minUv) * float2(size);
    query.maxExtentPixels = max(max(extent.x, extent.y), 1.0f);
    if (!(query.nearestDepth > 0.0f && query.nearestDepth < 1.0f)) return false;
    query.rejectReason = 0u;
    return true;
}

// D32 の丸めと clip 演算に共通の余裕を設ける。固定 .0005 の遠距離過剰 bias を廃止。
float HikariDepthOcclusionBias(float nearestDepth)
{
    return max(1e-7f, abs(nearestDepth) * (8.0f * 1.192092896e-7f));
}

bool HikariDepthOccluded(Texture2D<float> hierarchy, HikariDepthOcclusionQuery query,
    uint2 mip0Size, uint mipCount, float additionalBias)
{
    if (mipCount == 0u || any(mip0Size == 0u)) return false;
    uint mip = min((uint)ceil(log2(max(query.maxExtentPixels, 1.0f))), mipCount - 1u);
    const float limit = query.nearestDepth - HikariDepthOcclusionBias(query.nearestDepth) - max(additionalBias, 0.0f);
    // 粗い 2x2 が空/遠景で失敗した時だけ最大 2 mip 細分化する。
    // 各細分化は最大 4x4、総 load 上限 36。矩形内を全数検査し、穴を飛び越えない。
    [loop] for (uint level = 0u; level < 3u; ++level)
    {
        uint2 size = max(mip0Size >> min(mip, 31u), 1u.xx);
        int2 lo = min(int2(floor(query.minUv * float2(size))), int2(size) - 1);
        int2 hi = min(int2(floor(query.maxUv * float2(size))), int2(size) - 1);
        uint maxSpan = level == 0u ? 2u : 4u;
        if (any(lo < 0) || any(hi < lo) || any(hi - lo >= int(maxSpan))) return false;
        bool hidden = true;
        [loop] for (int y = lo.y; y <= hi.y && hidden; ++y)
        [loop] for (int x = lo.x; x <= hi.x; ++x)
        {
            float depth = hierarchy.Load(int3(x, y, mip));
            if (!isfinite(depth) || depth < 0.0f || !(depth < limit)) { hidden = false; break; }
        }
        if (hidden) return true;
        if (mip == 0u) break;
        --mip;
    }
    return false;
}

#endif
