#ifndef HIKARI_SURFACE_FEATURE_FLOATS_INCLUDED
#define HIKARI_SURFACE_FEATURE_FLOATS_INCLUDED

// ROOT_PARAM::SurfaceFeatureFloats (t1, space7)。地形 field と水面 coverage が
// 共有する単一 float pool。offset 0 は 4 個の zero を指す neutral 値。
StructuredBuffer<float> gSurfaceFeatureFloats : register(t1, space7);

// 正方形格子 (gridResolution+1)^2 の field を uv で双線形サンプルする。
// uv は [0,1] へ丸め、端は外側の頂点を繰り返す。
struct HikariSurfaceFeatureCorners
{
    uint i00;
    uint i10;
    uint i01;
    uint i11;
    float2 t;
};

HikariSurfaceFeatureCorners HikariResolveSurfaceFeatureCorners(
    uint gridResolution,
    float2 uv)
{
    const float2 f = saturate(uv) * (float)gridResolution;
    const uint x0 = min((uint)f.x, gridResolution);
    const uint y0 = min((uint)f.y, gridResolution);
    const uint x1 = min(x0 + 1u, gridResolution);
    const uint y1 = min(y0 + 1u, gridResolution);
    const uint stride = gridResolution + 1u;
    HikariSurfaceFeatureCorners corners;
    corners.i00 = y0 * stride + x0;
    corners.i10 = y0 * stride + x1;
    corners.i01 = y1 * stride + x0;
    corners.i11 = y1 * stride + x1;
    corners.t = f - float2((float)x0, (float)y0);
    return corners;
}

float HikariSampleSurfaceFeatureField(
    uint offset,
    uint gridResolution,
    float2 uv)
{
    const HikariSurfaceFeatureCorners c =
        HikariResolveSurfaceFeatureCorners(gridResolution, uv);
    return lerp(
        lerp(gSurfaceFeatureFloats[offset + c.i00], gSurfaceFeatureFloats[offset + c.i10], c.t.x),
        lerp(gSurfaceFeatureFloats[offset + c.i01], gSurfaceFeatureFloats[offset + c.i11], c.t.x),
        c.t.y);
}

#endif
