#ifndef HIKARI_FLUID_GRID
#define HIKARI_FLUID_GRID
// 体積流体の格子（交错 MAC 格子）。速度の xyz は各格の下側の面（x 面は x = i）、w は格の中心の温度。速度の体積は
// 各辺 n + 1 個（上側の境界面まで持つ）、密度・圧力は n 個。格の中心は格子座標 i + 0.5（単位は格）。
// 解算（Render3D_FluidSimulation.hlsl）と描画が同じ座標の約束で読む。

// 格子座標 p の速度（格/秒ではなく m/s）。成分ごとに面の位置へずらして三線形で読む。
float3 HikariFluidVelocityAt(Texture3D<float4> velocity, SamplerState linearClamp, float3 p, float3 velocityTexels)
{
    const float u = velocity.SampleLevel(linearClamp, (p + float3(0.5f, 0.0f, 0.0f)) / velocityTexels, 0).x;
    const float v = velocity.SampleLevel(linearClamp, (p + float3(0.0f, 0.5f, 0.0f)) / velocityTexels, 0).y;
    const float w = velocity.SampleLevel(linearClamp, (p + float3(0.0f, 0.0f, 0.5f)) / velocityTexels, 0).z;
    return float3(u, v, w);
}

// 格の中心に置いた量（密度、速度体積の w の温度）を格子座標 p で読む。texels は読む体積の大きさ。
float4 HikariFluidCellAt(Texture3D<float4> field, SamplerState sampler, float3 p, float3 texels)
{
    return field.SampleLevel(sampler, p / texels, 0);
}

// 源の表の一つ。座標は域の局所空間 [m]（格子の中心が原点）から源の局所空間へ toSource で移し、源の局所の向きは
// toDomain（回転だけ）で域へ戻す。C++ 側の FluidSourceGpu と同じ並び。
struct HikariFluidSource
{
    float4 toSource0;
    float4 toSource1;
    float4 toSource2;
    float4 toDomain0;
    float4 toDomain1;
    float4 toDomain2;
    float4 shape;    // x 形（FluidSourceShape）、y 半径、z 内径、w 高さ（円柱・線の長さ）
    float4 shape2;   // xyz 箱の半分の大きさ、w 円錐の角度 [rad]
    float4 emission; // x 毎秒の注入密度、y この子步の burst 密度、z 温度、w 汇（毎秒の吸収率）
    float4 velocity; // xyz 注入する速度（源の局所）、w 放射方向の速さ
    float4 field0;   // xyz 力場の中心（源の局所）、w 半径（0 で全体）
    float4 field1;   // xyz 軸、w 旋涡
    float4 field2;   // x 乱流、y 周波数、z 演化、w 吸引
    float4 wind;     // xyz 一様な加速度（域の局所）、w 速度の減衰 [1/s]
    float4 guide;    // x 形状引导の強さ、y 帯の厚さ、z 力場の効き方（0 加速度、1 目標速度）、w 縁の柔らかさ（源の局所）
    float4 flow;     // x 体積の湧き出し率 [1/s]（形の中で一様、開いた境界の域だけ）、y 注入の揺らぎ、z 1 / 揺らぎの塊 [1/m]、w 未使用
    float4 sweep;    // xyz この子步の間に動いた分（源の局所、終わりの形から始めの位置へ）、w 掃いた形へ入れる注入の割合
    float4 bounds;   // xyz 届く範囲の中心（域の局所）、w 半径（0 で域全体）
    float4 color;    // rgb 注入する烟の色（格ごとの色の域は密度 × 色を足す）、w 未使用
};

// 流体を押すもの 1 つ。C++ の FluidInteractionGpu と同じ並び。座標は域の局所空間 [m]。
struct HikariFluidInteraction
{
    float4 toLocal0;  // 域の局所 → 形の局所（回転と平行移動）の 3x4 の行
    float4 toLocal1;
    float4 toLocal2;
    float4 extents;   // xyz 形の大きさ（FluidInteraction.extents）、w 種類（0 障碍物、1 冲量、2 力）
    float4 velocity;  // xyz 障碍物の域に対する速度か冲量・力の向き（域の局所、0 で外向き）、w 形（0 球、1 カプセル、2 箱）
    float4 push;      // xyz 中心（域の局所）、w 冲量の速さ [m/s] か力の加速度 [m/s²]（注入はこの子步の割合を掛けた毎秒の密度）
    float4 sweep;     // 注入：xyz このフレームに動いた線分（中心から始めの位置へ、域の局所）、w 掃いた形へ入れる割合
};

uint HikariFluidInteractionKind(HikariFluidInteraction interaction)
{
    return (uint)(interaction.extents.w + 0.5f);
}

// 障碍物の形の符号付き距離（形の局所、内側が負）。
float HikariFluidInteractionDistance(HikariFluidInteraction interaction, float3 p)
{
    const uint shape = (uint)(interaction.velocity.w + 0.5f);
    if (shape == 0u)
        return length(p) - interaction.extents.x;
    if (shape == 1u)
    {
        p.y -= clamp(p.y, -interaction.extents.y, interaction.extents.y);
        return length(p) - interaction.extents.x;
    }
    const float3 q = abs(p) - interaction.extents.xyz;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

float3 HikariFluidToSource(HikariFluidSource source, float3 p)
{
    const float4 h = float4(p, 1.0f);
    return float3(dot(source.toSource0, h), dot(source.toSource1, h), dot(source.toSource2, h));
}

float3 HikariFluidToDomain(HikariFluidSource source, float3 v)
{
    return float3(dot(source.toDomain0.xyz, v), dot(source.toDomain1.xyz, v), dot(source.toDomain2.xyz, v));
}

// 源の形の符号付き距離（源の局所、内側が負）。形の種類は VFX の emission shape と同じ並び、7 は域全体。
float HikariFluidSourceDistance(HikariFluidSource source, float3 p)
{
    const uint shape = (uint)(source.shape.x + 0.5f);
    const float radius = source.shape.y;
    if (shape == 1u)
    {
        // 内径があれば球殻（内径〜半径の帯）。
        const float r = length(p);
        return source.shape.z > 0.0f ? max(r - radius, source.shape.z - r) : r - radius;
    }
    if (shape == 2u)
    {
        // 円錐：頂点が原点、+Y 方向へ長さ radius、半角 shape2.w。
        const float slope = tan(clamp(source.shape2.w, 0.0f, 1.55f));
        return max(max(-p.y, p.y - radius), length(p.xz) - slope * max(p.y, 0.0f));
    }
    if (shape == 3u)
    {
        const float3 q = abs(p) - source.shape2.xyz;
        return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
    }
    if (shape == 4u)
    {
        const float2 d = float2(length(p.xz) - radius, abs(p.y) - 0.5f * source.shape.w);
        return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
    }
    if (shape == 5u)
    {
        // 円環の帯（XZ 平面、内径〜半径）。厚さは縁の柔らかさの 2 倍。
        const float center = 0.5f * (source.shape.z + radius);
        const float halfWidth = max(0.5f * (radius - source.shape.z), source.guide.w);
        const float2 d = float2(abs(length(p.xz) - center) - halfWidth, abs(p.y) - source.guide.w);
        return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
    }
    if (shape == 6u)
    {
        const float halfLength = 0.5f * source.shape.w;
        return length(p - float3(clamp(p.x, -halfLength, halfLength), 0.0f, 0.0f)) - max(radius, source.guide.w);
    }
    if (shape == 7u)
        return -1.0e6f;
    return length(p) - max(radius, source.guide.w);
}

// 掃いた線分（原点から d）の上で点 p に一番近い位置から p への差。動いた源・注入の形を、線分の一番近い位置に置いて測る。
float3 HikariFluidFromSegment(float3 p, float3 d)
{
    const float lengthSquared = dot(d, d);
    return p - (lengthSquared > 0.0f ? saturate(dot(p, d) / lengthSquared) : 0.0f) * d;
}

// 子步の間に掃いた形の符号付き距離（源の局所）：終わりの形から始めの位置へ戻る線分の上で、点に一番近い位置の形。
float HikariFluidSweptDistance(HikariFluidSource source, float3 p)
{
    return HikariFluidSourceDistance(source, HikariFluidFromSegment(p, source.sweep.xyz));
}

// 掃いた形の中の重み（内側 1、表面の外へ縁の柔らかさの幅で 0 へ）に、掃いた形へ入れる割合を掛けたもの（止まった源は 1）。
float HikariFluidSourceMask(HikariFluidSource source, float3 p)
{
    return saturate(0.5f - HikariFluidSweptDistance(source, p) / max(source.guide.w, 1.0e-4f)) * source.sweep.w;
}

// 域の局所の点 p に源が届くか（届く範囲の外の格は源を読まない）。
bool HikariFluidSourceReaches(HikariFluidSource source, float3 p)
{
    return source.bounds.w <= 0.0f || length(p - source.bounds.xyz) <= source.bounds.w;
}

#endif
