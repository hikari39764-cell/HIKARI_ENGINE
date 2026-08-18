#ifndef HIKARI_SAND_SURFACE_DEFORM_INCLUDED
#define HIKARI_SAND_SURFACE_DEFORM_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"

float HikariSandSampleSurfaceHeight(
    HikariSandSurfaceParams p,
    float2 localXZ)
{
    const float2 uv =
        localXZ / p.extents + 0.5f;
    const float totalHeight = HikariSandSampleHeight(p, uv);
    const float ripple = HikariEvaluateSandRippleHeight(
        localXZ,
        p.rippleAmount,
        p.heightScale);
    return totalHeight + ripple;
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
