#ifndef HIKARI_VOLUMETRIC_CLOUD_NOISE_INCLUDED
#define HIKARI_VOLUMETRIC_CLOUD_NOISE_INCLUDED

#ifndef HIKARI_NOISE_INCLUDED
#include "Include/Math/HIKARI_Noise.hlsli"
#endif

// 体積雲の手続きノイズ。どれも単位立方体（正方形）の周期でつながり、wrap で敷き詰めても継ぎ目が出ない。
// ここでは生の値を返す。Render3D_VolumetricCloudNoise.hlsl が GPU のヒストグラムで一様分布へ写す。

float HikariCloudWorleyFbm3D(float3 uv, int frequency, uint salt)
{
    return HikariNoisePeriodicCellular3D(uv * frequency, int3(frequency, frequency, frequency), salt) * 0.625f +
        HikariNoisePeriodicCellular3D(uv * frequency * 2, int3(frequency * 2, frequency * 2, frequency * 2),
            salt ^ 0x5bd1e995u) * 0.25f +
        HikariNoisePeriodicCellular3D(uv * frequency * 4, int3(frequency * 4, frequency * 4, frequency * 4),
            salt ^ 0x1b873593u) * 0.125f;
}

float HikariCloudPerlinFbm3D(float3 uv, int frequency, uint salt)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        const int octaveFrequency = frequency << octave;
        sum += amplitude * HikariNoisePeriodicGradient3D(uv * octaveFrequency,
            int3(octaveFrequency, octaveFrequency, octaveFrequency), salt + uint(octave) * 0x9e3779b9u);
        total += amplitude;
        amplitude *= 0.5f;
    }
    return saturate(sum / total * 0.5f + 0.5f);
}

float HikariCloudValueFbm2D(float2 uv, int2 frequency, uint salt, int octaves = 4)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    [unroll]
    for (int octave = 0; octave < octaves; ++octave)
    {
        const int2 octaveFrequency = frequency << octave;
        sum += amplitude * HikariNoisePeriodicValue2D(uv * float2(octaveFrequency), octaveFrequency,
            salt + uint(octave) * 0x85ebca6bu);
        total += amplitude;
        amplitude *= 0.5f;
    }
    return sum / total;
}

// 雲の塊。Perlin の連続した広がりを Worley の膨らみで持ち上げ、低い周波数の Worley で縁を膨らませる。
float HikariCloudShapeField(float3 uv)
{
    const float perlin = HikariCloudPerlinFbm3D(uv, 4, 0x31a7c9edu);
    const float worley = HikariCloudWorleyFbm3D(uv, 4, 0x9e3779b9u);
    const float perlinWorley = saturate(worley + perlin * (1.0f - worley));
    const float lowFrequency = worley * 0.625f + HikariCloudWorleyFbm3D(uv, 8, 0x243f6a88u) * 0.25f +
        HikariCloudWorleyFbm3D(uv, 16, 0x6d2b79f5u) * 0.125f;
    return saturate((perlinWorley - lowFrequency + 1.0f) / (2.0f - lowFrequency));
}

// 縁を侵食する細かい凹凸。
float HikariCloudDetailField(float3 uv)
{
    return HikariCloudWorleyFbm3D(uv, 2, 0x85ebca6bu) * 0.625f + HikariCloudWorleyFbm3D(uv, 4, 0xc2b2ae35u) * 0.25f +
        HikariCloudWorleyFbm3D(uv, 8, 0x27d4eb2fu) * 0.125f;
}

// 格子の特徴点ごとにガウスの山（高さ 0.4〜1、半径 0.35〜0.7 格子）を置いた和（3x3 近傍）。隣り合う山は
// 滑らかにつながり、等高線は丸い雲の輪郭になる（距離場のような多角形の辺は出ない）。
float HikariCloudBlobs2D(float2 position, int period, uint salt)
{
    const int2 baseCell = int2(floor(position));
    const float2 local = frac(position);
    float total = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const uint hash =
                HikariNoiseHashInt2(HikariNoiseWrapCell2(baseCell + int2(x, y), int2(period, period)), salt);
            const float2 feature = float2(HikariNoiseHashUnit(hash),
                HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0xa511e9b3u)));
            const float amplitude = 0.4f + 0.6f * HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0x63d83595u));
            const float radius = 0.35f + 0.35f * HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0x2c1b3c6du));
            const float2 delta = float2(x, y) + feature - local;
            total += amplitude * exp(-dot(delta, delta) / (radius * radius));
        }
    }
    return total;
}

// 積雲の対流セル。周期あたり 11・30・90 個のセル（周期 40km でおよそ 3.6km・1.3km・0.45km）を値ノイズでゆがめて
// 混ぜる。値が高いほどセルの中心。山の和の分布は格子の細かさによらない。大きなセルが主で、幅広く平たい雲
// （晴天の積雲は高さより幅がずっと大きい）ができ、セルごとの半径と強さの乱数で大小の雲が混ざる。小さなセルは
// 大きな雲の凹凸と群れの縁の小さな雲にとどめる（多いと同じ大きさの小さな塊が空一面に散らばる）。
float HikariCloudCellField(float2 uv)
{
    const float2 warp = float2(HikariCloudValueFbm2D(uv, int2(8, 8), 0x2545f491u),
        HikariCloudValueFbm2D(uv, int2(8, 8), 0x9e3779b1u)) - 0.5f;
    const float2 warped = uv + warp * 0.017f;
    // 山の和は 0〜2 程度。一様化の前に 0..1 へ収める。
    return saturate((HikariCloudBlobs2D(warped * 11.0f, 11, 0x1b56c4e9u) * 0.65f +
        HikariCloudBlobs2D(warped * 30.0f, 30, 0x5e2d58d9u) * 0.27f +
        HikariCloudBlobs2D(warped * 90.0f, 90, 0x7a5d3c21u) * 0.08f) * 0.5f);
}

// 天気図。x 雲域の揺らぎ（大きな雲域と雲の群れ）、y 雲型の揺らぎ、z 対流セル、w 雲頂の塔（周期 40km で 830m から
// 3 段の値ノイズ。セルの中で雲頂を上下させ、1 つのセルがひとつの丸屋根にならない）。
float4 HikariCloudWeatherField(float2 uv)
{
    const float region = HikariCloudValueFbm2D(uv, int2(4, 4), 0x4f1bbcdcu);
    const float cells = HikariNoisePeriodicCellular2D(uv * 8.0f, int2(8, 8), 0x7f4a7c15u) * 0.65f +
        HikariNoisePeriodicCellular2D(uv * 16.0f, int2(16, 16), 0x94d049bbu) * 0.35f;
    const float type = HikariCloudValueFbm2D(uv, int2(2, 2), 0x369dea0fu);
    const float turret = HikariCloudValueFbm2D(uv, int2(48, 48), 0x7c3a9e5du, 3);
    return float4(0.6f * region + 0.4f * cells, type, HikariCloudCellField(uv), turret);
}

#endif
