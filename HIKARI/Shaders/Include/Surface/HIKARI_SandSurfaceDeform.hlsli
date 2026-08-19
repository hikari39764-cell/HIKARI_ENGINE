#ifndef HIKARI_SAND_SURFACE_DEFORM_INCLUDED
#define HIKARI_SAND_SURFACE_DEFORM_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"

float HikariSandSampleSurfaceHeight(
    HikariSandSurfaceParams p,
    float2 localXZ)
{
    const float2 uv =
        localXZ / p.extents + 0.5f;
    const float granularWeight =
        HikariSandSampleGranularWeight(p, uv);
    const float totalHeight = HikariSandSampleHeight(p, uv);
    const float ripple = HikariEvaluateSandRippleHeight(
        localXZ,
        p.rippleAmount * granularWeight,
        p.heightScale);
    return totalHeight + ripple;
}

float2 HikariResolveTerrainFacetSlope(
    HikariSandSurfaceParams p,
    float2 localXZ,
    float2 smoothSlope)
{
    const HikariSandLayerStyle style =
        HikariSandLoadLayerStyle(p, 0u);
    const float strength = saturate(style.facetStrength);
    if (strength <= 1.0e-5f)
    {
        return smoothSlope;
    }

    // 高密度 grid の各微小三角を flat 化せず、authoring された metre 尺度で
    // 仮想 coarse cell を作る。各 cell は 2 枚の大きな三角平面を持ち、
    // checkerboard diagonal で長い一方向の筋を避ける。
    const float requestedSize = max(style.facetSizeMeters, 0.25f);
    const float2 halfExtents = p.extents * 0.5f;
    const float2 gridPosition =
        max(localXZ + halfExtents, 0.0f.xx) / requestedSize;
    const float2 cellIndex = floor(gridPosition);
    const float2 cellMin = clamp(
        cellIndex * requestedSize - halfExtents,
        -halfExtents,
        halfExtents);
    const float2 cellMax = clamp(
        cellMin + requestedSize,
        -halfExtents,
        halfExtents);
    const float2 cellExtent = max(
        cellMax - cellMin,
        float2(1.0e-4f, 1.0e-4f));
    const float2 cellUv = saturate(
        (localXZ - cellMin) / cellExtent);

    const float2 uv00 = cellMin / p.extents + 0.5f;
    const float2 uv10 = float2(cellMax.x, cellMin.y) / p.extents + 0.5f;
    const float2 uv01 = float2(cellMin.x, cellMax.y) / p.extents + 0.5f;
    const float2 uv11 = cellMax / p.extents + 0.5f;
    const float h00 = HikariSandSampleHeight(p, uv00);
    const float h10 = HikariSandSampleHeight(p, uv10);
    const float h01 = HikariSandSampleHeight(p, uv01);
    const float h11 = HikariSandSampleHeight(p, uv11);

    float2 facetSlope = smoothSlope;
    const uint checker =
        ((uint)cellIndex.x + (uint)cellIndex.y) & 1u;
    if (checker == 0u)
    {
        facetSlope = cellUv.x + cellUv.y <= 1.0f
            ? float2(
                (h10 - h00) / cellExtent.x,
                (h01 - h00) / cellExtent.y)
            : float2(
                (h11 - h01) / cellExtent.x,
                (h11 - h10) / cellExtent.y);
    }
    else
    {
        facetSlope = cellUv.x <= cellUv.y
            ? float2(
                (h11 - h01) / cellExtent.x,
                (h01 - h00) / cellExtent.y)
            : float2(
                (h10 - h00) / cellExtent.x,
                (h11 - h10) / cellExtent.y);
    }
    return lerp(smoothSlope, facetSlope, strength);
}

void HikariApplySandSurfaceDeform(
    HikariSurfaceGpuSceneInstance instance,
    inout float3 localPosition,
    inout float3 localNormal,
    out float3 sandSurfaceData)
{
    sandSurfaceData = float3(localPosition.xz, 0.0f);
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_SAND_SURFACE) == 0u)
    {
        return;
    }

    const HikariSandSurfaceParams p =
        HikariLoadSandSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);
    const float cellX = p.extents.x / (float)max(p.gridResolution, 1u);
    const float cellZ = p.extents.y / (float)max(p.gridResolution, 1u);

    const float2 uv =
        localPosition.xz / p.extents + 0.5f;
    const float du = cellX / p.extents.x;
    const float dv = cellZ / p.extents.y;

    const float hL = HikariSandSampleHeight(p, uv - float2(du, 0.0f));
    const float hR = HikariSandSampleHeight(p, uv + float2(du, 0.0f));
    const float hD = HikariSandSampleHeight(p, uv - float2(0.0f, dv));
    const float hU = HikariSandSampleHeight(p, uv + float2(0.0f, dv));
    float slopeX = (hR - hL) / (2.0f * cellX);
    float slopeZ = (hU - hD) / (2.0f * cellZ);
    const float2 resolvedSlope = HikariResolveTerrainFacetSlope(
        p,
        localPosition.xz,
        float2(slopeX, slopeZ));
    slopeX = resolvedSlope.x;
    slopeZ = resolvedSlope.y;

    const float totalHeight = HikariSandSampleSurfaceHeight(
        p,
        localPosition.xz);
    // 着色へ渡すのは authored base / ripple / shoreline sink を含まない
    // runtime response だけ。負値が pit、正値が押し出された rim。
    const float runtimeDeformation =
        HikariSandSampleDeformation(p, uv);
    // 筆が保持する coverage 勾配をそのまま連続した縁の沈降へ写す。
    float edgeSink = 0.0f;
    {
        const float edge = HikariSandEdgeFactor(p, uv);
        const float depth = (p.edgeDepth > 1.0e-4f)
            ? p.edgeDepth
            : p.rimStrength;
        // edge(0=縁 .. 1=実体) を二乗イーズで滑らかな沈降プロファイルに。
        const float t = edge * edge * (3.0f - 2.0f * edge);
        edgeSink = -depth * (1.0f - t);

        const float eL = HikariSandEdgeFactor(p, uv - float2(du, 0.0f));
        const float eR = HikariSandEdgeFactor(p, uv + float2(du, 0.0f));
        const float eD = HikariSandEdgeFactor(p, uv - float2(0.0f, dv));
        const float eU = HikariSandEdgeFactor(p, uv + float2(0.0f, dv));
        const float sL = eL * eL * (3.0f - 2.0f * eL);
        const float sR = eR * eR * (3.0f - 2.0f * eR);
        const float sD = eD * eD * (3.0f - 2.0f * eD);
        const float sU = eU * eU * (3.0f - 2.0f * eU);
        slopeX += depth * (sR - sL) / (2.0f * cellX);
        slopeZ += depth * (sU - sD) / (2.0f * cellZ);
    }
    localPosition.y += totalHeight + edgeSink;
    localNormal = normalize(float3(-slopeX, 1.0f, -slopeZ));
    sandSurfaceData = float3(
        localPosition.xz,
        runtimeDeformation);
}

#endif
