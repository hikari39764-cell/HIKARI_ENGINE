#ifndef HIKARI_SURFACE_FEATURE_COVERAGE_INCLUDED
#define HIKARI_SURFACE_FEATURE_COVERAGE_INCLUDED

bool HikariSurfaceHasMaterialFx(uint surfaceFlags)
{
    return
        (surfaceFlags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) != 0u &&
        (surfaceFlags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX) == 0u;
}

bool HikariSurfaceHasWaterMaterialFx(uint surfaceFlags)
{
    return
        (surfaceFlags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) != 0u &&
        (surfaceFlags & HIKARI_SURFACE_GPU_SCENE_FLAG_WATER_MATERIAL_FX) != 0u;
}

float HikariSurfaceFeatureHash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float HikariSurfaceFeatureNoise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);

    float n000 = HikariSurfaceFeatureHash31(i + float3(0.0f, 0.0f, 0.0f));
    float n100 = HikariSurfaceFeatureHash31(i + float3(1.0f, 0.0f, 0.0f));
    float n010 = HikariSurfaceFeatureHash31(i + float3(0.0f, 1.0f, 0.0f));
    float n110 = HikariSurfaceFeatureHash31(i + float3(1.0f, 1.0f, 0.0f));
    float n001 = HikariSurfaceFeatureHash31(i + float3(0.0f, 0.0f, 1.0f));
    float n101 = HikariSurfaceFeatureHash31(i + float3(1.0f, 0.0f, 1.0f));
    float n011 = HikariSurfaceFeatureHash31(i + float3(0.0f, 1.0f, 1.0f));
    float n111 = HikariSurfaceFeatureHash31(i + float3(1.0f, 1.0f, 1.0f));

    float3 u = f * f * (3.0f - 2.0f * f);
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

struct HikariSurfaceFeatureCoverage
{
    float noise;
    float edge;
    float edgeBand;
};

HikariSurfaceFeatureCoverage HikariEvaluateStaticMaterialFxCoverage(
    float3 worldPosition,
    float4 fxUser0,
    float4 fxUser1)
{
    HikariSurfaceFeatureCoverage coverage = (HikariSurfaceFeatureCoverage)0;
    const float dissolveAmount = saturate(fxUser0.z);
    const float edgeWidth = max(fxUser0.w, 0.0001f);
    const float noiseScale = max(fxUser1.z, 0.0001f);

    coverage.noise = HikariSurfaceFeatureNoise3D(worldPosition * noiseScale);
    coverage.edge = smoothstep(
        dissolveAmount,
        dissolveAmount + edgeWidth,
        coverage.noise);
    coverage.edgeBand =
        smoothstep(dissolveAmount - edgeWidth, dissolveAmount, coverage.noise) -
        smoothstep(dissolveAmount, dissolveAmount + edgeWidth, coverage.noise);

    if (coverage.noise < dissolveAmount)
    {
        discard;
    }
    return coverage;
}

void HikariApplyStaticMaterialFxCoverage(
    float3 worldPosition,
    float4 fxUser0,
    float4 fxUser1)
{
    HikariSurfaceFeatureCoverage coverage =
        HikariEvaluateStaticMaterialFxCoverage(
        worldPosition,
        fxUser0,
        fxUser1);
}

#endif
