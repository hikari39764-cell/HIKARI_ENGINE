#ifndef HIKARI_FLUID_MEDIUM
#define HIKARI_FLUID_MEDIUM
// 体積流体の介質：描画する域の記録と、ある点での消光・散乱・発光。不透明面の步进（Render3D_FluidVolume.hlsl の
// MarchPS）と透明物体用の相機体積（CameraVolumeCS）が同じ関数で評価する。
#include "Include/Lighting/HIKARI_VolumetricScattering.hlsli"

// 描画する域 1 つ。C++ の FluidRenderDomainGpu と同じ並び。
struct HikariFluidDomain
{
    float4 toGrid0;   // 世界 → 格子座標（格、格子の角が原点）の 3x4 の行
    float4 toGrid1;
    float4 toGrid2;
    float4 cells;     // xyz 格の数、w 格の大きさ [m]
    float4 look;      // xyz 散乱の色（色の倍率込み）、w 密度 1 の消光 [1/m]
    float4 emission;  // xyz 発光の色 × 強さ、w 起亮温度
    float4 optics;    // x 各向異性、y 多重散乱、z 環境光の強さ、w 細部の強さ
    float4 detail;    // x 細部の模様の繰り返し [1/m]（1 周期に 4 つの模様）、y 細部の流れの時間 [s]、z 開いた境界で密度を消す縁の幅 [格]（0 で消さない）、w 未使用
    float4 sun;       // xyz 太陽の向き（格子座標、正規化）、w 未使用
};

float3 HikariFluidToGrid(HikariFluidDomain domain, float3 world)
{
    const float4 h = float4(world, 1.0f);
    return float3(dot(domain.toGrid0, h), dot(domain.toGrid1, h), dot(domain.toGrid2, h));
}

// 格子座標の線分 origin + direction * t が箱 [0, cells] を通る区間。通らなければ near > far。
float2 HikariFluidBoxInterval(float3 origin, float3 direction, float3 cells)
{
    const float3 inverse = 1.0f / select(abs(direction) > 1.0e-8f, direction, 1.0e-8f.xxx);
    const float3 a = (0.0f.xxx - origin) * inverse;
    const float3 b = (cells - origin) * inverse;
    const float3 lower = min(a, b);
    const float3 upper = max(a, b);
    return float2(max(max(lower.x, lower.y), lower.z), min(min(upper.x, upper.y), upper.z));
}

bool HikariFluidInside(float3 grid, float3 cells) { return all(grid >= 0.0f) && all(grid <= cells); }

// 開いた境界の域は外へ出ていく烟が域の面で切れて箱の形が見えないよう、面に近い格で密度を 0 へ寄せる。
float HikariFluidEdgeFade(HikariFluidDomain domain, float3 grid)
{
    if (domain.detail.z <= 0.0f)
        return 1.0f;
    const float3 edge = min(grid, domain.cells.xyz - grid);
    return smoothstep(0.0f, domain.detail.z, min(edge.x, min(edge.y, edge.z)));
}

// 多重散乱の近似（Wrenninge 2013 の 3 段）：段ごとに寄与・消光・各向異性を半分にして足し、足りない分を (1 + multiple)
// 倍で補う。multiple が 1 で体積雲（HikariCloudLightScattering）と同じ形、0 で単一散乱。
float HikariFluidSunScattering(float cosTheta, float anisotropy, float multiple, float sunTransmittance)
{
    float result = 0.0f;
    float b = 1.0f;
    float c = 1.0f;
    [unroll] for (uint octave = 0u; octave < 3u; ++octave)
    {
        const float a = octave == 0u ? 1.0f : multiple * exp2(-(float)octave);
        result += a * HikariHenyeyGreenstein(cosTheta, anisotropy * c) * pow(max(sunTransmittance, 1.0e-6f), b);
        b *= 0.5f;
        c *= 0.5f;
    }
    return result * (1.0f + multiple);
}

// 空の環境光が届く割合：上への透過率を、多重散乱の分だけ深くまで届かせる（multiple 1 で平方根）。
float HikariFluidSkyVisibility(float skyTransmittance, float multiple)
{
    return pow(max(skyTransmittance, 1.0e-6f), 1.0f - 0.5f * saturate(multiple));
}

// 温度から発光（起亮温度で 0、そこから 1 だけ上で全強度）。
float3 HikariFluidEmission(HikariFluidDomain domain, float temperature)
{
    return domain.emission.xyz * saturate(temperature - domain.emission.w);
}

#endif
