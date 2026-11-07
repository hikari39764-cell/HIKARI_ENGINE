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
    float4 emissionHot; // xyz 熱い端の色 × 強さ、w 熱い端の温度（起亮温度以下なら一色）
    float4 optics;    // x 各向異性、y 多重散乱、z 環境光の強さ、w 細部の強さ
    float4 detail;    // x 細部の模様の繰り返し [1/m]（1 周期に 4 つの模様）、y 細部の流れの時間 [s]、z 開いた境界で密度を消す縁の幅 [格]（0 で消さない）、w 最後の子步の後に貯まった時間 [s]（子步 1 つ以下。流れに沿って外推する分）
    float4 sun;       // xyz 太陽の向き（格子座標、正規化）、w 1 なら格ごとの色（密度の yzw が密度 × 色）
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

// 開いた面から外へ出ていく烟が面で切れて箱の形が見えないよう、開いた面に近い 4 格で密度を 0 へ寄せる。detail.z は
// 開いた面の印（bit 0 −x、1 +x、2 −y、3 +y、4 −z、5 +z）。閉じた面（床・壁）の近くは消さない。
float HikariFluidEdgeFade(HikariFluidDomain domain, float3 grid)
{
    const uint open = (uint)(domain.detail.z + 0.5f);
    if (open == 0u)
        return 1.0f;
    float edge = 4.0f;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (((open >> (axis * 2u)) & 1u) != 0u)
            edge = min(edge, grid[axis]);
        if (((open >> (axis * 2u + 1u)) & 1u) != 0u)
            edge = min(edge, domain.cells[axis] - grid[axis]);
    }
    return smoothstep(0.0f, 4.0f, edge);
}

// 何度も散乱して届く光の割合（拡散近似）：光学的な厚さ τ の烟を、まっすぐ来る光は exp(-τ) しか通らないが、散乱を
// 繰り返す光はおよそ 1 / (1 + 0.75 (1 - g) τ) 通る（厚い雲の下の曇り空が真っ暗にならない理由）。散乱する烟の無い所
// （透過率 1）では 0 にし、薄い烟の明るさは変えない。
float HikariFluidDiffuseLight(float transmittance, float anisotropy)
{
    const float clamped = clamp(transmittance, 1.0e-30f, 1.0f);
    return (1.0f - clamped) / (1.0f + 0.75f * (1.0f - anisotropy) * -log(clamped));
}

// 多重散乱の近似（Wrenninge 2013 の 3 段）：段ごとに寄与・消光・各向異性を半分にして足し、足りない分を (1 + multiple)
// 倍で補う。multiple が 1 で体積雲（HikariCloudLightScattering）と同じ形、0 で単一散乱。3 段でも厚い烟の奥（太陽への
// 光学的な厚さが 10 を超える所）は黒く沈むので、拡散近似の等方な光（multiple 倍）を下限にする（大きな烟の影の側と、
// 球壳の内側が真っ黒にならない）。足すと中くらいの影まで明るくなり、小さな烟の陰影が浅くなる。openness は周りへの
// 開き具合（近くの遮り）で、下限の光にだけ掛ける。
float HikariFluidSunScattering(float cosTheta, float anisotropy, float multiple, float sunTransmittance, float openness)
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
    return max(result * (1.0f + multiple),
               multiple * 0.0795775f * openness * HikariFluidDiffuseLight(sunTransmittance, anisotropy));
}

// 空の環境光が届く割合：上への透過率を、多重散乱の分だけ深くまで届かせる（multiple 1 で平方根）。厚い烟の奥は拡散近似
// の分（周りへの開き具合 openness を掛ける）を下限にする。
float HikariFluidSkyVisibility(float skyTransmittance, float multiple, float anisotropy, float openness)
{
    return max(pow(max(skyTransmittance, 1.0e-6f), 1.0f - 0.5f * saturate(multiple)),
               multiple * openness * HikariFluidDiffuseLight(skyTransmittance, anisotropy));
}

// 温度から発光（起亮温度で 0、そこから 1 だけ上で全強度）。色は起亮温度の emission から熱い端の温度の emissionHot へ
// 線形に変わる（火の赤 → 橙 → 白黄の芯）。
float3 HikariFluidEmission(HikariFluidDomain domain, float temperature)
{
    const float ignition = domain.emission.w;
    const float span = domain.emissionHot.w - ignition;
    const float t = span > 1.0e-4f ? saturate((temperature - ignition) / span) : 0.0f;
    return lerp(domain.emission.xyz, domain.emissionHot.xyz, t) * saturate(temperature - ignition);
}

#endif
