#ifndef HIKARI_TERRAIN_SURFACE_PARAMS_INCLUDED
#define HIKARI_TERRAIN_SURFACE_PARAMS_INCLUDED

#include "Include/Surface/HIKARI_SurfaceFeatureFloats.hlsli"

static const uint HIKARI_TERRAIN_SURFACE_GPU_SCHEMA_VERSION = 3u;
static const uint HIKARI_TERRAIN_SURFACE_GPU_FLAG_ENABLED = 1u << 0;
static const uint HIKARI_TERRAIN_LAYER_STYLE_FLOAT_COUNT = 20u;
static const uint HIKARI_TERRAIN_LAYER_MATERIAL_KIND_SAND = 0u;
static const uint HIKARI_TERRAIN_MAX_LAYERS = 8u;

// C++ 側の TerrainSurfaceGpuData と同一の sparse record layout。
struct HikariTerrainSurfaceGpuData
{
    uint4 header;        // schemaVersion, flags, gridResolution, layerCount (sentinel: record 数)
    uint4 fieldOffsets;  // height, slope, coverage, deformation
    uint4 layerOffsets;  // layerWeights, layerStyles, reserved, reserved
    float4 extentsScale; // extentX, extentZ, heightScale, rimStrength
    float4 styleLight;   // lightTint.rgb, sparkleStrength
    float4 styleDark;    // darkTint.rgb, rippleAmount
    float4 styleShade;   // pitDarkening, toonBandHardness, toonBandCount, wetness
    float4 envelope;     // boundsMinY, boundsMaxY, edgeDepth, reserved
};

// ROOT_PARAM::TerrainSurfaceData (t4, space7)。水面の t0 と分離し、
// 水面/地形が同じ pass に共存しても別々のバッファを束縛できるようにする。
StructuredBuffer<HikariTerrainSurfaceGpuData> gTerrainSurfaceData : register(t4, space7);

struct HikariTerrainSurfaceParams
{
    bool valid;
    uint gridResolution;
    uint layerCount;
    uint heightOffset;
    uint slopeOffset;
    uint coverageOffset;
    uint deformationOffset;
    uint layerWeightsOffset;
    uint layerStylesOffset;
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
    float wetness;
    float edgeDepth;
    float boundsMinY;
    float boundsMaxY;
};

HikariTerrainSurfaceParams HikariBuildNeutralTerrainSurfaceParams()
{
    HikariTerrainSurfaceParams p = (HikariTerrainSurfaceParams)0;
    p.gridResolution = 1u;
    p.layerCount = 1u;
    p.extents = float2(1.0f, 1.0f);
    p.heightScale = 1.0f;
    p.lightTint = 1.0f.xxx;
    p.toonBandHardness = 0.14f;
    p.toonBandCount = 3.0f;
    p.edgeDepth = 1.5f;
    return p;
}

HikariTerrainSurfaceParams HikariLoadTerrainSurfaceParams(
    uint surfaceFeatureDataIndex)
{
    HikariTerrainSurfaceParams p = HikariBuildNeutralTerrainSurfaceParams();
    const HikariTerrainSurfaceGpuData sentinel = gTerrainSurfaceData[0];
    const bool validEnvelope =
        sentinel.header.x == HIKARI_TERRAIN_SURFACE_GPU_SCHEMA_VERSION &&
        sentinel.header.y == 0u &&
        sentinel.header.w > 0u;
    if (!validEnvelope ||
        surfaceFeatureDataIndex == 0u ||
        surfaceFeatureDataIndex >= sentinel.header.w)
    {
        return p;
    }

    const HikariTerrainSurfaceGpuData data =
        gTerrainSurfaceData[surfaceFeatureDataIndex];
    if (data.header.x != HIKARI_TERRAIN_SURFACE_GPU_SCHEMA_VERSION ||
        (data.header.y & HIKARI_TERRAIN_SURFACE_GPU_FLAG_ENABLED) == 0u)
    {
        return p;
    }

    p.valid = true;
    p.gridResolution = max(data.header.z, 1u);
    p.layerCount = clamp(data.header.w, 1u, HIKARI_TERRAIN_MAX_LAYERS);
    p.heightOffset = data.fieldOffsets.x;
    p.slopeOffset = data.fieldOffsets.y;
    p.coverageOffset = data.fieldOffsets.z;
    p.deformationOffset = data.fieldOffsets.w;
    p.layerWeightsOffset = data.layerOffsets.x;
    p.layerStylesOffset = data.layerOffsets.y;
    p.extents = max(data.extentsScale.xy, float2(0.001f, 0.001f));
    p.heightScale = max(data.extentsScale.z, 0.0f);
    p.rimStrength = max(data.extentsScale.w, 0.0f);
    p.lightTint = saturate(data.styleLight.rgb);
    p.sparkleStrength = max(data.styleLight.w, 0.0f);
    p.darkTint = saturate(data.styleDark.rgb);
    p.rippleAmount = max(data.styleDark.w, 0.0f);
    p.pitDarkening = saturate(data.styleShade.x);
    p.toonBandHardness = clamp(data.styleShade.y, 0.0f, 0.5f);
    p.toonBandCount = clamp(data.styleShade.z, 2.0f, 4.0f);
    p.wetness = saturate(data.styleShade.w);
    p.boundsMinY = data.envelope.x;
    p.boundsMaxY = data.envelope.y;
    p.edgeDepth = clamp(data.envelope.z, 0.0f, 100.0f);
    return p;
}

struct HikariTerrainLayerStyle
{
    float3 lightTint;
    float textureStrength;
    float3 darkTint;
    float normalStrength;
    float4 atlasScaleBias;
    float tilesPerMeter;
    float roughness;
    float saturation;
    uint materialKind;
    float shadowStrength;
    float granularStrength;
};

// style は地形 core と同時に常駐する。neutral params の時だけ既定の砂地を返す。
HikariTerrainLayerStyle HikariTerrainLoadLayerStyle(
    HikariTerrainSurfaceParams p,
    uint layerIndex)
{
    HikariTerrainLayerStyle style = (HikariTerrainLayerStyle)0;
    if (p.layerStylesOffset == 0u)
    {
        style.lightTint = p.lightTint;
        style.textureStrength = 0.52f;
        style.darkTint = p.darkTint;
        style.normalStrength = 0.72f;
        style.atlasScaleBias = float4(0.4921875f, 0.4921875f, 0.00390625f, 0.00390625f);
        style.tilesPerMeter = 1.0f / 25.0f;
        style.roughness = 0.92f;
        style.saturation = 1.14f;
        style.shadowStrength = 1.0f;
        style.granularStrength = 1.0f;
        return style;
    }
    const uint base = p.layerStylesOffset +
        layerIndex * HIKARI_TERRAIN_LAYER_STYLE_FLOAT_COUNT;
    style.lightTint = float3(
        gSurfaceFeatureFloats[base + 0u],
        gSurfaceFeatureFloats[base + 1u],
        gSurfaceFeatureFloats[base + 2u]);
    style.textureStrength = gSurfaceFeatureFloats[base + 3u];
    style.darkTint = float3(
        gSurfaceFeatureFloats[base + 4u],
        gSurfaceFeatureFloats[base + 5u],
        gSurfaceFeatureFloats[base + 6u]);
    style.normalStrength = gSurfaceFeatureFloats[base + 7u];
    style.atlasScaleBias = float4(
        gSurfaceFeatureFloats[base + 8u],
        gSurfaceFeatureFloats[base + 9u],
        gSurfaceFeatureFloats[base + 10u],
        gSurfaceFeatureFloats[base + 11u]);
    style.tilesPerMeter = gSurfaceFeatureFloats[base + 12u];
    style.roughness = gSurfaceFeatureFloats[base + 13u];
    style.saturation = gSurfaceFeatureFloats[base + 14u];
    style.materialKind = (uint)gSurfaceFeatureFloats[base + 15u];
    style.shadowStrength = gSurfaceFeatureFloats[base + 16u];
    style.granularStrength = gSurfaceFeatureFloats[base + 17u];
    return style;
}

uint HikariTerrainLoadLayerMaterialKind(
    HikariTerrainSurfaceParams p,
    uint layerIndex)
{
    return p.layerStylesOffset == 0u
        ? HIKARI_TERRAIN_LAYER_MATERIAL_KIND_SAND
        : (uint)gSurfaceFeatureFloats[
            p.layerStylesOffset + layerIndex * HIKARI_TERRAIN_LAYER_STYLE_FLOAT_COUNT + 15u];
}

float HikariTerrainSampleCoverage(
    HikariTerrainSurfaceParams p,
    float2 uv)
{
    return p.coverageOffset == 0u
        ? 1.0f
        : HikariSampleSurfaceFeatureField(p.coverageOffset, p.gridResolution, uv);
}

// offset 0 は runtime 応答なし。
float HikariTerrainSampleDeformation(
    HikariTerrainSurfaceParams p,
    float2 uv)
{
    return p.deformationOffset == 0u
        ? 0.0f
        : HikariSampleSurfaceFeatureField(p.deformationOffset, p.gridResolution, uv);
}

// CPU 焼き込み済みの最終高さ (ripple と縁の沈降を含む)。
float HikariTerrainSampleHeight(
    HikariTerrainSurfaceParams p,
    float2 uv)
{
    return HikariSampleSurfaceFeatureField(p.heightOffset, p.gridResolution, uv);
}

float2 HikariTerrainDecodeSlope(float packedSlope)
{
    const uint bits = asuint(packedSlope);
    return float2(f16tof32(bits & 0xFFFFu), f16tof32(bits >> 16u));
}

// CPU 焼き込み済みの勾配 (dY/dX, dY/dZ)。
float2 HikariTerrainSampleSlope(
    HikariTerrainSurfaceParams p,
    float2 uv)
{
    const HikariSurfaceFeatureCorners c =
        HikariResolveSurfaceFeatureCorners(p.gridResolution, uv);
    const uint o = p.slopeOffset;
    return lerp(
        lerp(HikariTerrainDecodeSlope(gSurfaceFeatureFloats[o + c.i00]),
            HikariTerrainDecodeSlope(gSurfaceFeatureFloats[o + c.i10]), c.t.x),
        lerp(HikariTerrainDecodeSlope(gSurfaceFeatureFloats[o + c.i01]),
            HikariTerrainDecodeSlope(gSurfaceFeatureFloats[o + c.i11]), c.t.x),
        c.t.y);
}

float4 HikariTerrainDecodeLayerWeights(float packedWeights)
{
    const uint bits = asuint(packedWeights);
    return float4(
        (float)(bits & 0xFFu),
        (float)((bits >> 8u) & 0xFFu),
        (float)((bits >> 16u) & 0xFFu),
        (float)(bits >> 24u)) * (1.0f / 255.0f);
}

float4 HikariTerrainSampleLayerWeightPlane(
    HikariSurfaceFeatureCorners c,
    uint planeOffset)
{
    return lerp(
        lerp(HikariTerrainDecodeLayerWeights(gSurfaceFeatureFloats[planeOffset + c.i00]),
            HikariTerrainDecodeLayerWeights(gSurfaceFeatureFloats[planeOffset + c.i10]), c.t.x),
        lerp(HikariTerrainDecodeLayerWeights(gSurfaceFeatureFloats[planeOffset + c.i01]),
            HikariTerrainDecodeLayerWeights(gSurfaceFeatureFloats[planeOffset + c.i11]), c.t.x),
        c.t.y);
}

// 全層の weight を一度に読む。layers 0..3 は weights0、4..7 は weights1。
void HikariTerrainSampleLayerWeights(
    HikariTerrainSurfaceParams p,
    float2 uv,
    out float4 weights0,
    out float4 weights1)
{
    weights0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
    weights1 = 0.0f.xxxx;
    if (p.layerCount <= 1u || p.layerWeightsOffset == 0u)
    {
        return;
    }
    const HikariSurfaceFeatureCorners c =
        HikariResolveSurfaceFeatureCorners(p.gridResolution, uv);
    weights0 = HikariTerrainSampleLayerWeightPlane(c, p.layerWeightsOffset);
    if (p.layerCount > 4u)
    {
        const uint planeFloats = (p.gridResolution + 1u) * (p.gridResolution + 1u);
        weights1 = HikariTerrainSampleLayerWeightPlane(
            c, p.layerWeightsOffset + planeFloats);
    }
}

float HikariTerrainLayerWeight(float4 weights0, float4 weights1, uint layer)
{
    return saturate(layer < 4u ? weights0[layer] : weights1[layer - 4u]);
}

// coverage 自体が筆の hardness/falloff を保持する。全 pass が同じ双線形値を使う。
float HikariTerrainEdgeFactor(HikariTerrainSurfaceParams p, float2 uv)
{
    return saturate(HikariTerrainSampleCoverage(p, uv));
}

void HikariClipTerrainCoverage(HikariTerrainSurfaceParams p, float2 uv)
{
    // CPU query / imprint の可視 coverage 閾値と同値。
    // edge sink がほぼ完了した位置まで平滑斜面を残す。
    static const float kVisibleCoverageThreshold = 0.05f;
    clip(HikariTerrainEdgeFactor(p, uv) - kVisibleCoverageThreshold);
}

float HikariTerrainHash21(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    float mixed = dot(p3, p3.yzx + 33.33f) * 1.61803398875f;
    return frac(mixed);
}

float HikariTerrainValueNoise(float2 noisePoint)
{
    float2 cell = floor(noisePoint);
    float2 local = noisePoint - cell;
    float2 fade = local * local * (3.0f - 2.0f * local);
    float a = HikariTerrainHash21(cell);
    float b = HikariTerrainHash21(cell + float2(1.0f, 0.0f));
    float c = HikariTerrainHash21(cell + float2(0.0f, 1.0f));
    float d = HikariTerrainHash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, fade.x), lerp(c, d, fade.x), fade.y);
}

#endif
