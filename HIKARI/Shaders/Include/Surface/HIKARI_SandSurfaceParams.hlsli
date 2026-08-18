#ifndef HIKARI_SAND_SURFACE_PARAMS_INCLUDED
#define HIKARI_SAND_SURFACE_PARAMS_INCLUDED

static const uint HIKARI_SAND_SURFACE_GPU_SCHEMA_VERSION = 2u;
static const uint HIKARI_SAND_SURFACE_GPU_FLAG_ENABLED = 1u << 0;
static const uint HIKARI_SAND_SURFACE_GPU_LAYER_COUNT_SHIFT = 1u;
static const uint HIKARI_SAND_SURFACE_GPU_LAYER_COUNT_MASK = 0x000000FEu;
static const uint HIKARI_SAND_SURFACE_GPU_DEFORMATION_OFFSET_SHIFT = 8u;
static const uint HIKARI_SAND_SURFACE_GPU_DEFORMATION_OFFSET_MASK = 0xFFFFFF00u;

// C++ 側の SandSurfaceGpuData と同一の sparse record layout。
struct HikariSandSurfaceGpuData
{
    uint4 header;          // schemaVersion, flags, gridResolution, heightOffset
    float4 extentsScale;   // extentX, extentZ, heightScale, rimStrength
    float4 styleLight;     // lightTint.rgb, sparkleStrength
    float4 styleDark;      // darkTint.rgb, rippleAmount
    float4 stylePit;       // pitDarkening, reserved, reserved, reserved
    float4 styleWet;       // wetness, reserved, reserved, reserved
};

// ROOT_PARAM::SandSurfaceData (t4, space7)。水面の t0 と分離し、
// 水面/沙地が同じ pass に共存しても別々のバッファを束縛できるようにする。
StructuredBuffer<HikariSandSurfaceGpuData> gSandSurfaceData : register(t4, space7);
// ROOT_PARAM::SandSurfaceHeights (t1, space7)。名前は互換のため維持し、
// total height / deformation / coverage / layer payload の単一 float pool とする。
StructuredBuffer<float> gSandSurfaceHeights : register(t1, space7);

struct HikariSandSurfaceParams
{
    uint gridResolution;
    uint heightOffset;
    uint deformationOffset;
    float2 extents;
    float heightScale;
    float rimStrength;
    float3 lightTint;
    float sparkleStrength;
    float3 darkTint;
    float rippleAmount;
    float pitDarkening;
    float toonBandHardness;
    float toonBandCount;
    uint layerCount;
    uint layerWeightsOffset;
    uint coverageOffset;
    float wetness;
    uint layerTintsOffset;
    float edgeDepth;
};

HikariSandSurfaceParams HikariBuildNeutralSandSurfaceParams()
{
    HikariSandSurfaceParams p = (HikariSandSurfaceParams)0;
    p.gridResolution = 1u;
    p.extents = float2(1.0f, 1.0f);
    p.heightScale = 1.0f;
    p.lightTint = 1.0f.xxx;
    p.darkTint = 0.0f.xxx;
    p.toonBandHardness = 0.14f;
    p.toonBandCount = 3.0f;
    p.layerCount = 1u;
    p.deformationOffset = 0u;
    p.layerWeightsOffset = 0u;
    p.coverageOffset = 0u;
    p.wetness = 0.0f;
    p.edgeDepth = 1.5f;
    return p;
}

HikariSandSurfaceParams HikariLoadSandSurfaceParamsFromDataIndex(
    uint surfaceFeatureDataIndex)
{
    HikariSandSurfaceParams p = HikariBuildNeutralSandSurfaceParams();
    const HikariSandSurfaceGpuData sentinel = gSandSurfaceData[0];
    const bool validEnvelope =
        sentinel.header.x == HIKARI_SAND_SURFACE_GPU_SCHEMA_VERSION &&
        sentinel.header.y == 0u &&
        sentinel.header.w > 0u;
    if (!validEnvelope ||
        surfaceFeatureDataIndex == 0u ||
        surfaceFeatureDataIndex >= sentinel.header.w)
    {
        return p;
    }

    const HikariSandSurfaceGpuData data =
        gSandSurfaceData[surfaceFeatureDataIndex];
    if (data.header.x != HIKARI_SAND_SURFACE_GPU_SCHEMA_VERSION ||
        (data.header.y & HIKARI_SAND_SURFACE_GPU_FLAG_ENABLED) == 0u)
    {
        return p;
    }

    p.gridResolution = max(data.header.z, 1u);
    p.heightOffset = data.header.w;
    p.extents = max(data.extentsScale.xy, float2(0.001f, 0.001f));
    p.heightScale = max(data.extentsScale.z, 0.0f);
    p.rimStrength = max(data.extentsScale.w, 0.0f);
    p.lightTint = saturate(data.styleLight.rgb);
    p.sparkleStrength = max(data.styleLight.w, 0.0f);
    p.darkTint = saturate(data.styleDark.rgb);
    p.rippleAmount = max(data.styleDark.w, 0.0f);
    p.pitDarkening = saturate(data.stylePit.x);
    p.toonBandHardness = clamp(data.stylePit.y, 0.0f, 0.5f);
    p.toonBandCount = clamp(data.stylePit.z, 2.0f, 4.0f);
    // styleWet.w は edgeDepth。flags は layerCount(bits 1..7) と
    // deformationOffset(bits 8..31) を共有する。
    p.edgeDepth = clamp(data.styleWet.w, 0.0f, 100.0f);
    p.layerCount = max(
        (data.header.y & HIKARI_SAND_SURFACE_GPU_LAYER_COUNT_MASK) >>
            HIKARI_SAND_SURFACE_GPU_LAYER_COUNT_SHIFT,
        1u);
    p.deformationOffset =
        (data.header.y & HIKARI_SAND_SURFACE_GPU_DEFORMATION_OFFSET_MASK) >>
        HIKARI_SAND_SURFACE_GPU_DEFORMATION_OFFSET_SHIFT;
    p.layerWeightsOffset = (uint)data.stylePit.w;
    p.coverageOffset = (uint)data.styleWet.y;
    p.wetness = saturate(data.styleWet.x);
    p.layerTintsOffset = (uint)data.styleWet.z;
    return p;
}

// 層 tint を float プールから読む。layer ごとに light.rgb + dark.rgb の 6 float。
// プール未供給 (offset==0) のときは従来ハードコード色へフォールバックし、
// 組み込み sand/snow/dirt の見た目を完全に保つ。
float3 HikariSandSampleLayerLightTint(
    HikariSandSurfaceParams p,
    uint layerIndex)
{
    if (p.layerTintsOffset == 0u)
    {
        if (layerIndex == 0u) { return p.lightTint; }
        if (layerIndex == 1u) { return float3(1.0f, 1.0f, 1.0f); }
        return float3(0.52f, 0.38f, 0.24f);
    }
    const uint base = p.layerTintsOffset + layerIndex * 6u;
    return float3(
        gSandSurfaceHeights[base + 0u],
        gSandSurfaceHeights[base + 1u],
        gSandSurfaceHeights[base + 2u]);
}

float3 HikariSandSampleLayerDarkTint(
    HikariSandSurfaceParams p,
    uint layerIndex)
{
    if (p.layerTintsOffset == 0u)
    {
        if (layerIndex == 0u) { return p.darkTint; }
        if (layerIndex == 1u) { return float3(0.68f, 0.78f, 0.90f); }
        return float3(0.28f, 0.20f, 0.12f);
    }
    const uint base = p.layerTintsOffset + layerIndex * 6u;
    return float3(
        gSandSurfaceHeights[base + 3u],
        gSandSurfaceHeights[base + 4u],
        gSandSurfaceHeights[base + 5u]);
}

float HikariSandSampleCoverage(
    HikariSandSurfaceParams p,
    float2 uv)
{
    if (p.coverageOffset == 0u)
    {
        return 1.0f;
    }
    const float2 safeUv = saturate(uv);
    const float fu = safeUv.x * (float)p.gridResolution;
    const float fv = safeUv.y * (float)p.gridResolution;
    uint x0 = (uint)fu;
    uint y0 = (uint)fv;
    uint x1 = min(x0 + 1u, p.gridResolution);
    uint y1 = min(y0 + 1u, p.gridResolution);
    const float tx = fu - (float)x0;
    const float ty = fv - (float)y0;
    const uint stride = p.gridResolution + 1u;
    const uint base = p.coverageOffset;
    const float c00 = gSandSurfaceHeights[base + y0 * stride + x0];
    const float c10 = gSandSurfaceHeights[base + y0 * stride + x1];
    const float c01 = gSandSurfaceHeights[base + y1 * stride + x0];
    const float c11 = gSandSurfaceHeights[base + y1 * stride + x1];
    return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
}

float HikariSandSampleLayerWeight(
    HikariSandSurfaceParams p,
    uint layerIndex,
    float2 uv)
{
    if (p.layerCount <= 1u || p.layerWeightsOffset == 0u)
    {
        return layerIndex == 0u ? 1.0f : 0.0f;
    }
    const float2 safeUv = saturate(uv);
    const float fu = safeUv.x * (float)p.gridResolution;
    const float fv = safeUv.y * (float)p.gridResolution;
    uint x0 = (uint)fu;
    uint y0 = (uint)fv;
    uint x1 = min(x0 + 1u, p.gridResolution);
    uint y1 = min(y0 + 1u, p.gridResolution);
    const float tx = fu - (float)x0;
    const float ty = fv - (float)y0;
    const uint stride = p.gridResolution + 1u;
    const uint cellCount = stride * stride;
    const uint base = p.layerWeightsOffset + layerIndex * cellCount;
    const float w00 = gSandSurfaceHeights[base + y0 * stride + x0];
    const float w10 = gSandSurfaceHeights[base + y0 * stride + x1];
    const float w01 = gSandSurfaceHeights[base + y1 * stride + x0];
    const float w11 = gSandSurfaceHeights[base + y1 * stride + x1];
    return lerp(lerp(w00, w10, tx), lerp(w01, w11, tx), ty);
}

float HikariSandSampleHeight(
    HikariSandSurfaceParams p,
    float2 uv)
{
    const float2 safeUv = saturate(uv);
    const float fu = safeUv.x * (float)p.gridResolution;
    const float fv = safeUv.y * (float)p.gridResolution;
    uint x0 = (uint)fu;
    uint y0 = (uint)fv;
    uint x1 = min(x0 + 1u, p.gridResolution);
    uint y1 = min(y0 + 1u, p.gridResolution);
    const float tx = fu - (float)x0;
    const float ty = fv - (float)y0;
    const uint stride = p.gridResolution + 1u;
    const float h00 = gSandSurfaceHeights[p.heightOffset + y0 * stride + x0];
    const float h10 = gSandSurfaceHeights[p.heightOffset + y0 * stride + x1];
    const float h01 = gSandSurfaceHeights[p.heightOffset + y1 * stride + x0];
    const float h11 = gSandSurfaceHeights[p.heightOffset + y1 * stride + x1];
    return lerp(
        lerp(h00, h10, tx),
        lerp(h01, h11, tx),
        ty);
}


// coverage 自体が筆の hardness/falloff を保持する。ここで別の侵食を掛けると
// 表示輪郭だけが筆位置からずれるため、全 pass が同じ双線形値を使う。
float HikariSandEdgeFactor(HikariSandSurfaceParams p, float2 uv)
{
    return saturate(HikariSandSampleCoverage(p, uv));
}

float HikariSandSampleDeformation(
    HikariSandSurfaceParams p,
    float2 uv)
{
    // offset 0 points at the neutral sentinel and means no runtime response.
    if (p.deformationOffset == 0u)
    {
        return 0.0f;
    }
    const float2 safeUv = saturate(uv);
    const float fu = safeUv.x * (float)p.gridResolution;
    const float fv = safeUv.y * (float)p.gridResolution;
    const uint x0 = (uint)fu;
    const uint y0 = (uint)fv;
    const uint x1 = min(x0 + 1u, p.gridResolution);
    const uint y1 = min(y0 + 1u, p.gridResolution);
    const float tx = fu - (float)x0;
    const float ty = fv - (float)y0;
    const uint stride = p.gridResolution + 1u;
    const uint base = p.deformationOffset;
    const float d00 = gSandSurfaceHeights[base + y0 * stride + x0];
    const float d10 = gSandSurfaceHeights[base + y0 * stride + x1];
    const float d01 = gSandSurfaceHeights[base + y1 * stride + x0];
    const float d11 = gSandSurfaceHeights[base + y1 * stride + x1];
    return lerp(
        lerp(d00, d10, tx),
        lerp(d01, d11, tx),
        ty);
}

void HikariClipSandCoverage(HikariSandSurfaceParams p, float2 uv)
{
    // CPU query / imprint の kSandSurfaceVisibleCoverageThreshold と同値。
    // edge sink がほぼ完了した位置まで平滑斜面を残す。
    static const float kVisibleCoverageThreshold = 0.05f;
    clip(HikariSandEdgeFactor(p, uv) - kVisibleCoverageThreshold);
}

float HikariSandHash21(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    float mixed = dot(p3, p3.yzx + 33.33f) * 1.61803398875f;
    return frac(mixed);
}

float HikariSandValueNoise(float2 noisePoint)
{
    float2 cell = floor(noisePoint);
    float2 local = noisePoint - cell;
    float2 fade = local * local * (3.0f - 2.0f * local);
    float a = HikariSandHash21(cell);
    float b = HikariSandHash21(cell + float2(1.0f, 0.0f));
    float c = HikariSandHash21(cell + float2(0.0f, 1.0f));
    float d = HikariSandHash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, fade.x), lerp(c, d, fade.x), fade.y);
}

float HikariEvaluateSandRippleHeight(
    float2 localXZ,
    float rippleAmount,
    float heightScale)
{
    const float safeRipple = clamp(rippleAmount, 0.0f, 3.0f);
    const float safeScale = clamp(heightScale, 0.0f, 2.0f);
    const float ripple =
        sin(localXZ.x * 0.85f + 1.7f * sin(localXZ.y * 0.21f)) *
            0.012f * safeRipple +
        sin(localXZ.y * 0.63f - 0.9f * sin(localXZ.x * 0.17f)) *
            0.009f * safeRipple;
    return ripple * safeScale;
}

#endif
