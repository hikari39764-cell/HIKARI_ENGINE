#ifndef HIKARI_SAND_SURFACE_PARAMS_INCLUDED
#define HIKARI_SAND_SURFACE_PARAMS_INCLUDED

static const uint HIKARI_SAND_SURFACE_GPU_SCHEMA_VERSION = 1u;
static const uint HIKARI_SAND_SURFACE_GPU_FLAG_ENABLED = 1u << 0;

// C++ 側の SandSurfaceGpuData と同一の sparse record layout。
struct HikariSandSurfaceGpuData
{
    uint4 header;          // schemaVersion, flags, gridResolution, heightOffset
    float4 extentsScale;   // extentX, extentZ, heightScale, rimStrength
    float4 styleLight;     // lightTint.rgb, sparkleStrength
    float4 styleDark;      // darkTint.rgb, rippleAmount
    float4 stylePit;       // pitDarkening, reserved, reserved, reserved
};

// ROOT_PARAM::SandSurfaceData (t4, space7)。水面の t0 と分離し、
// 水面/沙地が同じ pass に共存しても別々のバッファを束縛できるようにする。
StructuredBuffer<HikariSandSurfaceGpuData> gSandSurfaceData : register(t4, space7);
// ROOT_PARAM::SandSurfaceHeights (t1, space7)
StructuredBuffer<float> gSandSurfaceHeights : register(t1, space7);

struct HikariSandSurfaceParams
{
    uint gridResolution;
    uint heightOffset;
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
    return p;
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
