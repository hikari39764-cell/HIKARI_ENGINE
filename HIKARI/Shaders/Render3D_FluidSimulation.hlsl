// 体積流体の解算（VFX の流体層）。1 子步：平流（MacCormack）→ 旋度 → 力（源・力場・浮力・涡度约束）→ 散度 →
// 多重网格の V 循環で圧力 → 投影。格子は Include/Vfx/HIKARI_FluidGrid.hlsli の交错 MAC 格子。
// 圧力の方程式は A P = Σ近傍 P - k P = D h²（k は壁でない近傍の数、開いた境界の外は P = 0）、速度は
// u -= (P_i - P_{i-1}) / h。粗い段の右辺は細かい段の残差の平均の 4 倍（格の大きさが 2 倍になる分）。
#include "Include/Vfx/HIKARI_FluidGrid.hlsli"
#include "Include/Vfx/HIKARI_ParticleForceMath.hlsli"

cbuffer FluidDomainCB : register(b0)
{
    uint4 gSize;          // xyz 格の数、w 源の数
    float4 gCell;         // x 格の大きさ h [m]、y 1/h、z 子步の時間 [s]、w 作り直してからの解算時間 [s]
    float4 gMotion;       // x 閉じた面の印（FluidClosedFaces）、y 涡度约束、z 浮力（温度 1 あたり）、w 重さ（密度 1 あたり）
    float4 gDissipation;  // x 密度の消散 [1/s]、y 温度の消散 [1/s]
    float4 gUp;           // xyz 域の局所の上向き
    float4 gInertia;      // xyz 挂点の加速度の逆向き（域の局所 [m/s²]）
    // x 押すもの（障碍物・冲量・力・注入）の数、y gStats の中の砖块の活動状態の位置 [byte]、z 汇の吸い込み量の位置
    // [byte]（表の行ごとに float）、w 1 でこの子步の頭に吸い込み量を 0 に戻す、2 でこの子步の吸い込みを数える（預熱は数えない）
    uint4 gInteractionCount;
    // 燃焼（NVIDIA Flow の燃焼模型）：x 点火温度、y 燃える速さ [1/s]（温度が点火温度を 1 超えた時）、z 燃焼 1 あたりの燃料、
    // w 燃焼 1 あたりの温度の上がり / x 燃焼 1 あたりの煤（密度）、y 燃える速さ 1 あたりの体積の膨張、z 1 で燃焼する域。
    float4 gCombustion0;
    float4 gCombustion1;
};

cbuffer FluidPassCB : register(b1)
{
    uint4 gPassSize;      // xyz この pass が回す格子（段の格の数、切片の画素数）
    uint4 gPassParams;    // x 赤黒の色（発光：格の色を持つ域なら 1）、y 切片の量（発光：起亮温度）、z 切片の軸（発光：熱い端の温度）、w 切片の位置（格）か統計の位置
};

Texture3D<float4> gIn0 : register(t0);
Texture3D<float4> gIn1 : register(t1);
Texture3D<float4> gIn2 : register(t2);
Texture3D<float4> gIn3 : register(t3);
StructuredBuffer<HikariFluidSource> gSources : register(t6);
StructuredBuffer<HikariFluidInteraction> gInteractions : register(t7);
RWTexture3D<float4> gOut0 : register(u0);
RWTexture3D<float4> gOut1 : register(u1);
// 燃料（RG16F：x 燃料、y この子步の燃える速さ [1/s]）。燃焼の無い域では 1 格の置き場で、読み書きしない。
RWTexture3D<float2> gOut2 : register(u2);
RWTexture2D<float4> gSlice : register(u4);
RWByteAddressBuffer gStats : register(u5);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearBorder : register(s1);

// 閉じた面（bit 0 −x、1 +x、2 −y、3 +y、4 −z、5 +z）。多重網格のどの段でも同じ印で境界を決める（段ごとに算子が違うと
// 粗い段の修正が細かい段と食い違って発散する）。
uint ClosedFaces() { return (uint)(gMotion.x + 0.5f); }
bool ClosedFace(uint axis, bool upper) { return ((ClosedFaces() >> (axis * 2u + (upper ? 1u : 0u))) & 1u) != 0u; }
bool AllClosed() { return ClosedFaces() == 63u; }
bool Combusting() { return gCombustion1.z > 0.5f; }

// 格の中心の烟を格子座標 p で読む（x 密度、yzw 密度 × 色。格ごとの色を持たない域は R16 なので yzw は 0）。閉じた面は
// 壁の外を壁の値で延ばし（量を失わない）、開いた面は外を 0 にする（外から戻ってくる烟は無い）：閉じた面の側だけ p を
// 端の格の中心へ寄せ、外が 0 の標本器で読む。
float4 SmokeAt(Texture3D<float4> density, float3 p)
{
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (ClosedFace(axis, false))
            p[axis] = max(p[axis], 0.5f);
        if (ClosedFace(axis, true))
            p[axis] = min(p[axis], float(gSize[axis]) - 0.5f);
    }
    return HikariFluidCellAt(density, gLinearBorder, p, float3(gSize.xyz));
}
float DensityAt(Texture3D<float4> density, float3 p) { return SmokeAt(density, p).x; }
float3 Cells() { return float3(gSize.xyz); }
float3 VelocityTexels() { return float3(gSize.xyz + 1u); }
bool InsideCells(uint3 cell) { return all(cell < gSize.xyz); }
bool InsideFaces(uint3 face) { return all(face <= gSize.xyz); }

// ---------------------------------------------------------------- 疎な解算
// 砖块（8³ 格）ごとの活動状態（ActivityCS が子步ごとに書く、1 で動いている）。動いていない砖块では重い処理（MacCormack の
// 平流、旋度、源と力）を飛ばし、速度は前のまま写し、密度は 0 にする。散度・圧力・投影は域全体で解くので、烟の無い所の
// 空気も圧力で押されて非圧縮のまま動く（自分の流れでは運ばれず、力も受けない）。
static const uint kWorkPassive = 0u;
static const uint kWorkCompute = 1u;

uint3 BrickGrid() { return (gSize.xyz + 7u) / 8u; }
uint BrickIndex(uint3 brick) { return brick.x + BrickGrid().x * (brick.y + BrickGrid().y * brick.z); }

uint CellWork(uint3 cell)
{
    return (gStats.Load(gInteractionCount.y + BrickIndex(cell >> 3u) * 4u) & 1u) != 0u ? kWorkCompute : kWorkPassive;
}

uint StrongerWork(uint a, uint b) { return max(a, b); }

// 面の格子 face（格 face の下側の 3 面）の処理：その面を共有する格（face と、各軸で 1 つ下の格）のどれかが動いていれば解く。
uint FaceWork(uint3 face)
{
    uint work = InsideCells(face) ? CellWork(face) : kWorkPassive;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        int3 below = int3(face);
        below[axis] -= 1;
        if (below[axis] >= 0 && all(below < int3(gSize.xyz)))
            work = StrongerWork(work, CellWork(uint3(below)));
    }
    return work;
}


// 格子座標 p の速度（格の中心は cell + 0.5）。
float3 VelocityAt(Texture3D<float4> velocity, float3 p)
{
    return HikariFluidVelocityAt(velocity, gLinearClamp, p, VelocityTexels());
}

// 格 cell の中心の速度（両側の面の平均）。
float3 CellVelocity(Texture3D<float4> velocity, uint3 cell)
{
    const float3 lower = velocity.Load(int4(cell, 0)).xyz;
    return 0.5f * (lower + float3(velocity.Load(int4(cell + uint3(1, 0, 0), 0)).x,
                                  velocity.Load(int4(cell + uint3(0, 1, 0), 0)).y,
                                  velocity.Load(int4(cell + uint3(0, 0, 1), 0)).z));
}

// 格子座標 → 域の局所空間 [m]（格子の中心が原点）。
float3 DomainPosition(float3 p) { return (p - 0.5f * Cells()) * gCell.x; }

// 面の格子座標（成分 axis の面は、その軸だけ格の境界、他の軸は格の中心）。
float3 FacePosition(uint3 face, uint axis)
{
    float3 p = float3(face) + 0.5f;
    p[axis] -= 0.5f;
    return p;
}

// 面の値を位置 p で読む三線形の 8 標本の最小と最大（MacCormack の限幅）。
void FaceRange(Texture3D<float4> field, uint axis, float3 p, out float low, out float high)
{
    float3 t = p;
    t[axis] += 0.5f;
    const int3 base = int3(floor(t - 0.5f));
    const int3 limit = int3(gSize.xyz);
    low = 1.0e30f;
    high = -1.0e30f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const int3 index = clamp(base + int3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u), 0, limit);
        const float value = field.Load(int4(index, 0))[axis];
        low = min(low, value);
        high = max(high, value);
    }
}

// 格の中心の量（チャンネル channel）の 8 標本の最小と最大。texels は体積の大きさ。
void CellRange(Texture3D<float4> field, uint channel, float3 p, int3 limit, out float low, out float high)
{
    const int3 base = int3(floor(p - 0.5f));
    low = 1.0e30f;
    high = -1.0e30f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const int3 index = clamp(base + int3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u), 0, limit);
        const float value = field.Load(int4(index, 0))[channel];
        low = min(low, value);
        high = max(high, value);
    }
}

// ---------------------------------------------------------------- 作り直し
// u0 の体積を 0 にする（gPassSize の範囲）。
[numthreads(4, 4, 4)]
void ClearCS(uint3 id : SV_DispatchThreadID)
{
    if (all(id < gPassSize.xyz))
        gOut0[id] = 0.0f.xxxx;
}

// ---------------------------------------------------------------- 平流
// 形状引导の速度 [m/s]（域の局所の点 p）：源の形の外へ帯の厚さの中まで出た烟を形の中へ引き戻す（形の中の烟は動かさない。
// 中の烟まで表面へ寄せると、厚い帯が表面の 2 枚の薄い膜になり、寄せ合う所で半ラグランジュ平流が量を失う）。烟（密度）の平流だけに足し、
// 流れ（速度）には入れない。表面へ向かう流れは勾配の場で、速度へ入れると投影に打ち消され、圧力の残差だけが残って流れを
// 乱す（128 格では烟が消える）。強さ guide.x [1/s] × 表面までの距離で寄せ、帯の端で 0 になる。
float3 GuideVelocity(float3 p)
{
    float3 guide = 0.0f.xxx;
    for (uint index = 0u; index < gSize.w; ++index)
    {
        const HikariFluidSource source = gSources[index];
        if (source.guide.x <= 0.0f || !HikariFluidSourceReaches(source, p))
            continue;
        const float3 local = HikariFluidToSource(source, p);
        const float distance = HikariFluidSourceDistance(source, local);
        const float thickness = max(source.guide.y, 1.0e-3f);
        if (distance <= 0.0f || distance >= thickness)
            continue;
        const float e = 0.25f * thickness;
        const float3 gradient = float3(
            HikariFluidSourceDistance(source, local + float3(e, 0, 0)) - HikariFluidSourceDistance(source, local - float3(e, 0, 0)),
            HikariFluidSourceDistance(source, local + float3(0, e, 0)) - HikariFluidSourceDistance(source, local - float3(0, e, 0)),
            HikariFluidSourceDistance(source, local + float3(0, 0, e)) - HikariFluidSourceDistance(source, local - float3(0, 0, e)));
        const float3 normal = HikariFluidToDomain(source, gradient / max(length(gradient), 1.0e-6f));
        guide -= normal * (distance * source.guide.x * (1.0f - distance / thickness));
    }
    return guide;
}

// 前進：速度 t0（面 + 温度）と密度 t1 を速度で後ろへたどって読み、u0 / u1 へ書く（密度は形状引导の速度も足す）。
[numthreads(4, 4, 4)]
void AdvectForwardCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
    if (FaceWork(id) != kWorkCompute)
    {
        gOut0[id] = gIn0.Load(int4(id, 0));
        if (InsideCells(id))
        {
            gOut1[id] = 0.0f.xxxx;
            if (Combusting())
                gOut2[id] = 0.0f.xx;
        }
        return;
    }
    const float step = gCell.z * gCell.y;
    float4 velocity = 0.0f.xxxx;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        float3 extent = float3(gSize.xyz);
        extent[axis] += 1.0f;
        if (any(float3(id) >= extent))
            continue;
        const float3 p = FacePosition(id, axis);
        const float3 back = p - step * VelocityAt(gIn0, p);
        float3 t = back;
        t[axis] += 0.5f;
        velocity[axis] = gIn0.SampleLevel(gLinearClamp, t / VelocityTexels(), 0)[axis];
    }
    if (InsideCells(id) && CellWork(id) == kWorkCompute)
    {
        const float3 p = float3(id) + 0.5f;
        const float3 u = VelocityAt(gIn0, p);
        const float3 back = p - step * u;
        velocity.w = gIn0.SampleLevel(gLinearClamp, back / VelocityTexels(), 0).w;
        gOut1[id] = SmokeAt(gIn1, p - step * (u + GuideVelocity(DomainPosition(p))));
        // 燃料も烟と同じ率（densityDissipation）で薄れる（燃え残りが域に溜まり続けない。NVIDIA Flow の fuel damping）。
        if (Combusting())
            gOut2[id] = float2(SmokeAt(gIn2, back).x * exp(-gDissipation.x * gCell.z), 0.0f);
    }
    else if (InsideCells(id))
    {
        velocity.w = gIn0.Load(int4(id, 0)).w;
        gOut1[id] = 0.0f.xxxx;
        if (Combusting())
            gOut2[id] = 0.0f.xx;
    }
    gOut0[id] = velocity;
}

// 修正：t0 元の速度、t1 元の密度、t2 前進の速度、t3 前進の密度。前進の結果を進む向きにたどり直した値との差の半分で
// 誤差を打ち消し、後ろへたどった位置の元の 8 標本の範囲を外れたら前進の結果へ戻す。消散もここで掛ける。密度は形状引导の
// 速度を足した流れでたどる。
[numthreads(4, 4, 4)]
void AdvectCorrectCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
    if (FaceWork(id) != kWorkCompute)
    {
        gOut0[id] = gIn0.Load(int4(id, 0));
        if (InsideCells(id))
            gOut1[id] = 0.0f.xxxx;
        return;
    }
    const float step = gCell.z * gCell.y;
    float4 velocity = 0.0f.xxxx;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        float3 extent = float3(gSize.xyz);
        extent[axis] += 1.0f;
        if (any(float3(id) >= extent))
            continue;
        const float3 p = FacePosition(id, axis);
        const float3 u = VelocityAt(gIn0, p);
        float3 ahead = p + step * u;
        ahead[axis] += 0.5f;
        const float forward = gIn2.Load(int4(id, 0))[axis];
        const float corrected = forward + 0.5f * (gIn0.Load(int4(id, 0))[axis] -
                                                  gIn2.SampleLevel(gLinearClamp, ahead / VelocityTexels(), 0)[axis]);
        float low, high;
        FaceRange(gIn0, axis, p - step * u, low, high);
        velocity[axis] = corrected < low || corrected > high ? forward : corrected;
    }
    if (InsideCells(id) && CellWork(id) != kWorkCompute)
    {
        velocity.w = gIn0.Load(int4(id, 0)).w;
        gOut1[id] = 0.0f.xxxx;
    }
    else if (InsideCells(id))
    {
        const float3 p = float3(id) + 0.5f;
        const float3 u = VelocityAt(gIn0, p);
        const float3 back = p - step * u;
        const float3 ahead = p + step * u;
        float low, high;
        const float temperatureForward = gIn2.Load(int4(id, 0)).w;
        float temperature = temperatureForward + 0.5f * (gIn0.Load(int4(id, 0)).w -
                                                          gIn2.SampleLevel(gLinearClamp, ahead / VelocityTexels(), 0).w);
        CellRange(gIn0, 3u, back, int3(gSize.xyz) - 1, low, high);
        temperature = temperature < low || temperature > high ? temperatureForward : temperature;
        velocity.w = temperature * exp(-gDissipation.y * gCell.z);
        // 密度は前進と同じ「流れ + 形状引导」の速度でたどり直す。
        const float3 carried = u + GuideVelocity(DomainPosition(p));
        const float4 smokeForward = gIn3.Load(int4(id, 0));
        const float densityForward = smokeForward.x;
        float density = densityForward + 0.5f * (gIn1.Load(int4(id, 0)).x - DensityAt(gIn3, p + step * carried));
        CellRange(gIn1, 0u, p - step * carried, int3(gSize.xyz) - 1, low, high);
        density = max(density < low || density > high ? densityForward : density, 0.0f);
        // 色（密度 × 色）は格の色の比（色 / 密度）を保ったまま修正後の密度に掛ける。前進の密度が無い格（烟が空へ進む
        // 前沿）はこの格の前の値の比、それも無ければ白（比を固定の倍率で截断すると前沿の色が暗くなる）。
        const float4 previous = gIn1.Load(int4(id, 0));
        const float4 tinted = densityForward > 1.0e-6f ? smokeForward : previous;
        const float3 tint = tinted.x > 1.0e-6f ? tinted.yzw / tinted.x : 1.0f.xxx;
        gOut1[id] = float4(density, max(tint * density, 0.0f)) * exp(-gDissipation.x * gCell.z);
    }
    gOut0[id] = velocity;
}

// ---------------------------------------------------------------- 力
// 旋度（格の中心）：t0 速度 → u0（xyz 旋度、w 大きさ）。
[numthreads(4, 4, 4)]
void CurlCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideCells(id))
        return;
    if (CellWork(id) != kWorkCompute)
    {
        gOut0[id] = 0.0f.xxxx;
        return;
    }
    const int3 limit = int3(gSize.xyz) - 1;
    const float3 xp = CellVelocity(gIn0, uint3(min(int3(id) + int3(1, 0, 0), limit)));
    const float3 xn = CellVelocity(gIn0, uint3(max(int3(id) - int3(1, 0, 0), 0)));
    const float3 yp = CellVelocity(gIn0, uint3(min(int3(id) + int3(0, 1, 0), limit)));
    const float3 yn = CellVelocity(gIn0, uint3(max(int3(id) - int3(0, 1, 0), 0)));
    const float3 zp = CellVelocity(gIn0, uint3(min(int3(id) + int3(0, 0, 1), limit)));
    const float3 zn = CellVelocity(gIn0, uint3(max(int3(id) - int3(0, 0, 1), 0)));
    const float scale = 0.5f * gCell.y;
    const float3 curl = float3((yp.z - yn.z) - (zp.y - zn.y), (zp.x - zn.x) - (xp.z - xn.z),
                               (xp.y - xn.y) - (yp.x - yn.x)) * scale;
    gOut0[id] = float4(curl, length(curl));
}

// 格子点 c の 0〜1 の値（整数の hash）。
float LatticeValue(int3 c)
{
    uint h = (uint)c.x * 73856093u ^ (uint)c.y * 19349663u ^ (uint)c.z * 83492791u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return (float)((h ^ (h >> 16u)) & 0xffffu) / 65535.0f;
}

// 3D の値ノイズ（-1〜1、格子点の間を smoothstep で補間）。
float ValueNoise(float3 x)
{
    const int3 i = (int3)floor(x);
    const float3 f = x - floor(x);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float x00 = lerp(LatticeValue(i), LatticeValue(i + int3(1, 0, 0)), u.x);
    const float x10 = lerp(LatticeValue(i + int3(0, 1, 0)), LatticeValue(i + int3(1, 1, 0)), u.x);
    const float x01 = lerp(LatticeValue(i + int3(0, 0, 1)), LatticeValue(i + int3(1, 0, 1)), u.x);
    const float x11 = lerp(LatticeValue(i + int3(0, 1, 1)), LatticeValue(i + int3(1, 1, 1)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z) * 2.0f - 1.0f;
}

// 烟を注入する形の重み：子步の間に掃いた形の表面を格 4 つほどの塊で ±1.5 格ずらし、力の重み（HikariFluidSourceMask）の
// 2 倍の幅でぼかし、掃いた形へ入れる割合を掛ける。表面がそのまま格に沿った面で入ると、注入したての烟の縁が格子の階段
// （马赛克）に見える。形の奥（表面から 3 格より内側）の量は変わらない。p は域の局所 [m]。
float EmissionMask(HikariFluidSource source, float3 local, float3 p)
{
    const float soft = max(source.guide.w, 1.0e-4f);
    const float distance = HikariFluidSweptDistance(source, local);
    if (distance >= 2.5f * soft)
        return 0.0f;
    if (distance <= -2.5f * soft)
        return source.sweep.w;
    const float3 lump = p * (0.25f * gCell.y) + float3(0.0f, 0.07f, 0.0f) * gCell.w;
    const float offset = ValueNoise(lump) * 0.7f + ValueNoise(lump * 2.03f + 17.3f) * 0.3f;
    return saturate(0.5f - (distance + offset * soft) / (2.0f * soft)) * source.sweep.w;
}

// 注入の揺らぎ（FluidSourceSettings::emissionNoise）の倍率：域の局所 p [m] で塊の大きさ 1 / flow.z の値ノイズ 2 段により
// 注入量を揺らす（揺らぎ 1 で塊の間が所々 0、濃い塊は 2〜3 倍。平均はおよそ 1）。塊は 1 つ分を約 8 秒で流して形を変える。
float EmissionNoise(HikariFluidSource source, float3 p)
{
    if (source.flow.y <= 0.0f)
        return 1.0f;
    const float3 x = p * source.flow.z + float3(0.11f, 0.07f, 0.09f) * gCell.w;
    const float n = ValueNoise(x) * 0.65f + ValueNoise(x * 2.07f + 31.7f) * 0.35f;
    return max(1.0f + 2.5f * source.flow.y * n, 0.0f);
}

// 源 1 つの加速度と注入（格の中心、域の局所 p）。velocity は今の速度、smoke は烟（x 密度、yzw 密度 × 色）。
void ApplySource(HikariFluidSource source, float3 p, float3 velocity, inout float3 acceleration,
                 inout float4 smoke, inout float fuel, inout float temperature)
{
    if (!HikariFluidSourceReaches(source, p))
        return;
    const float3 local = HikariFluidToSource(source, p);
    const float mask = HikariFluidSourceMask(source, local);
    // 力場（旋涡・吸引・卷曲乱流）は粒子と同じ式。半径の減衰は式の中で掛かる。
    float3 field = 0.0f.xxx;
    if (source.field1.w != 0.0f || source.field2.x != 0.0f || source.field2.w != 0.0f)
    {
        // 旋涡と吸引は中心で向きが決まらない（粒子では問題にならない特異点）。流体では格子で表せない極小の渦核が
        // 生まれて量が増減するので、中心から約 2 格の内側を線形に弱める（Rankine の渦核）。
        const float core = saturate(length(local - source.field0.xyz) / max(1.5f * source.guide.w, 1.0e-4f));
        field = HikariFluidToDomain(source, ParticleForceAcceleration(local, gCell.w, source.field0.xyz,
            source.field0.w, source.field1.xyz, source.field1.w * core, source.field2.x, source.field2.y,
            source.field2.z, source.field2.w * core));
    }
    const float radius = source.field0.w;
    const float falloff = radius > 0.0f ? pow(saturate(1.0f - length(local - source.field0.xyz) / radius), 2.0f) : 1.0f;
    if (source.guide.z > 0.5f)
        acceleration += (field - velocity * falloff) * 4.0f;
    else
        acceleration += field;
    acceleration += source.wind.xyz * falloff - velocity * (source.wind.w * falloff);
    if (source.emission.x > 0.0f || source.emission.y > 0.0f)
    {
        const float emit = EmissionMask(source, local, p) * EmissionNoise(source, p);
        if (emit > 0.0f)
        {
            temperature += (source.emission.z - temperature) * saturate(emit * 8.0f * gCell.z + emit * saturate(source.emission.y));
            const float added = emit * (source.emission.x * gCell.z + source.emission.y);
            // 燃焼の域では注入量のうち color.w の割合が燃料（燃えて煤と熱になる）、残りが烟。
            const float burnable = Combusting() ? added * saturate(source.color.w) : 0.0f;
            fuel += burnable;
            smoke += (added - burnable) * float4(1.0f, source.color.rgb);
        }
    }
    if (mask <= 0.0f)
        return;
    // 注入する速度（源の局所の速度と放射方向）へ寄せる。
    const float3 injected = HikariFluidToDomain(source, source.velocity.xyz + source.velocity.w * normalize(local + 1.0e-6f));
    if (any(source.velocity != 0.0f))
        acceleration += (injected - velocity) * (8.0f * mask);
    const float absorbed = exp(-source.emission.w * mask * gCell.z);
    smoke *= absorbed;
    fuel *= absorbed;
}

// 燃焼 1 子步（NVIDIA Flow の combustSimulate と同じ形）：温度が点火温度を超えた格の燃料が、超えた分（1 まで）に比例した
// 速さで燃え（燃料で頭打ち）、燃えた量に比例して燃料が減り、温度が上がり、煤（白）が出る。戻り値は燃える速さ [1/s]
// （散度で体積を膨らませる）。
float Burn(inout float4 smoke, inout float fuel, inout float temperature)
{
    if (!Combusting() || fuel <= 0.0f || temperature <= gCombustion0.x)
        return 0.0f;
    float burn = gCombustion0.y * min(temperature - gCombustion0.x, 1.0f) * gCell.z;
    burn = min(burn, fuel / gCombustion0.z);
    fuel -= gCombustion0.z * burn;
    temperature += gCombustion0.w * burn;
    smoke += (gCombustion1.x * burn).xxxx;
    return burn / gCell.z;
}

// 押す強さの重み：半径の中で中心から二乗で弱まる。
float HikariFluidPushFalloff(HikariFluidInteraction interaction, float3 p)
{
    const float falloff = saturate(1.0f - length(p - interaction.push.xyz) / max(interaction.extents.x, 1.0e-4f));
    return falloff * falloff;
}

bool HikariFluidPushDirected(HikariFluidInteraction interaction)
{
    return dot(interaction.velocity.xyz, interaction.velocity.xyz) > 0.0f;
}

// 冲量（1）と力（2）だけが押す（障碍物 0 と注入 3 は別の扱い）。
bool HikariFluidIsPush(HikariFluidInteraction interaction)
{
    const uint kind = HikariFluidInteractionKind(interaction);
    return kind == 1u || kind == 2u;
}

// 力（2）が格 p の烟を消す率 [1/s]（中心から二乗で弱まる）。x 吸い込み：向きの無い負の力（汇）の Expansion の負の項
// （全部の面が閉じた箱では湧き出しを入れないので 0）。半拉格朗日の平流は集まる流れで濃さを変えないので、吸い込んだ体積
// ぶんの烟は消えるが、流れ込む烟は濃さを保ち、濃い烟の中の汇は烟を薄くしない。y 吸収（absorption）：半径の中の烟・
// 温度・燃料をこの率で直接減らす（NVIDIA Flow の目標 0 への結合、Blender の Outflow と同じ役）。
float2 HikariFluidRemovalRates(HikariFluidInteraction interaction, float3 p)
{
    if (HikariFluidInteractionKind(interaction) != 2u)
        return 0.0f;
    const float sink = HikariFluidPushDirected(interaction) || AllClosed()
                           ? 0.0f
                           : max(-interaction.push.w, 0.0f) * 30.0f / max(interaction.extents.x, 1.0e-4f);
    return float2(sink, interaction.sweep.x) * HikariFluidPushFalloff(interaction, p);
}

// gStats の float へ足す（比較交換の繰り返し）。
void AtomicAddFloat(uint address, float value)
{
    uint expected = gStats.Load(address);
    [loop] for (uint attempt = 0u; attempt < 64u; ++attempt)
    {
        uint original;
        gStats.InterlockedCompareExchange(address, expected, asuint(asfloat(expected) + value), original);
        if (original == expected)
            return;
        expected = original;
    }
}

// 力が烟を消す：吸収で烟・温度・燃料を減らし、回読があれば（gInteractionCount.w の bit 1）消えた量 [密度 × m³] を表の
// 行ごとに足す。消える量は 密度 × 格の体積 ×（1 - exp(-(吸い込み + 吸収) × 子步の時間)）（率が小さければ 率 × 時間、
// 強くても在る量を超えない）。波の中で足してから 1 回だけ書く。
void ApplySinks(float3 p, inout float4 smoke, inout float fuel, inout float temperature)
{
    const bool count = (gInteractionCount.w & 2u) != 0u;
    const float mass = smoke.x * gCell.x * gCell.x * gCell.x;
    float absorption = 0.0f;
    for (uint index = 0u; index < gInteractionCount.x; ++index)
    {
        const float2 rates = HikariFluidRemovalRates(gInteractions[index], p);
        absorption += rates.y;
        if (count)
        {
            const float removed = WaveActiveSum(mass * (1.0f - exp(-(rates.x + rates.y) * gCell.z)));
            if (WaveIsFirstLane() && removed > 0.0f)
                AtomicAddFloat(gInteractionCount.z + index * 4u, removed);
        }
    }
    const float kept = exp(-absorption * gCell.z);
    smoke *= kept;
    fuel *= kept;
    temperature *= kept;
}

// 注入の重み：このフレームに動いた線分（中心から sweep.xyz）からの距離で、半径の中で二乗に弱め、掃いた形へ入れる割合を掛ける。
float HikariFluidEmitFalloff(HikariFluidInteraction interaction, float3 p)
{
    const float distance = length(HikariFluidFromSegment(p - interaction.push.xyz, interaction.sweep.xyz));
    const float falloff = saturate(1.0f - distance / max(interaction.extents.x, 1.0e-4f));
    return falloff * falloff * interaction.sweep.w;
}

// 注入（3）：動いた線分を掃いた球の中（二乗で弱まる）へ毎秒 strength の密度を足し（色は白）、温度を extents.y へ寄せ、
// 向きがあれば注入する烟をその速さ（velocity.xyz、域の局所 [m/s]）へ寄せる（源の注入と同じ寄せ方）。
void ApplyEmit(HikariFluidInteraction interaction, float3 p, float3 velocity, inout float3 acceleration,
               inout float4 smoke, inout float temperature)
{
    const float weight = HikariFluidEmitFalloff(interaction, p);
    if (weight <= 0.0f)
        return;
    smoke += (max(interaction.push.w, 0.0f) * weight * gCell.z).xxxx;
    temperature += (interaction.extents.y - temperature) * saturate(weight * 8.0f * gCell.z);
    if (HikariFluidPushDirected(interaction))
        acceleration += (interaction.velocity.xyz - velocity) * (8.0f * weight);
}

// 向きのある冲量・力：その向きへ加速する（冲量は一子步で strength の速さを足す）。投影の後に回転の成分が残り、
// 烟の塊や渦の輪になる。向きの無い押しは Expansion（体積の湧き出し）で扱う。
void ApplyPush(HikariFluidInteraction interaction, float3 p, inout float3 acceleration)
{
    const float amount = interaction.push.w * HikariFluidPushFalloff(interaction, p);
    acceleration += interaction.velocity.xyz * (HikariFluidInteractionKind(interaction) == 1u ? amount / gCell.z : amount);
}

// 体積の湧き出し率 [1/s]（開いた境界の域だけ。閉じた箱では非圧縮の流体は逃げ場が無い）。負で吸い込む。
// 源の outflow は形の中で一様（率は C++ で表面積 / 体積を掛けてある）。向きの無い冲量・力は中心から二乗で弱まり、
// 半径の球面を外へ strength [m/s] で流れ出る量にする（4πR²S = ∫e dV、∫(1-r/R)² dV = 2πR³/15）。
float Expansion(float3 p)
{
    float expansion = 0.0f;
    for (uint source = 0u; source < gSize.w; ++source)
    {
        if (gSources[source].flow.x != 0.0f && HikariFluidSourceReaches(gSources[source], p))
            expansion += gSources[source].flow.x *
                         HikariFluidSourceMask(gSources[source], HikariFluidToSource(gSources[source], p));
    }
    for (uint index = 0u; index < gInteractionCount.x; ++index)
    {
        const HikariFluidInteraction interaction = gInteractions[index];
        if (!HikariFluidIsPush(interaction) || HikariFluidPushDirected(interaction))
            continue;
        expansion += 30.0f * interaction.push.w / max(interaction.extents.x, 1.0e-4f) *
                     HikariFluidPushFalloff(interaction, p);
    }
    return expansion;
}

// 障碍物：形の中（縁は格 1.5 個ぶん柔らかく）の加速度を、この子步で速度が障碍物の速度になる値へ置き換える。
// 他の力をすべて足した後に掛け、続く投影が回り込む流れを作る。
void ApplyObstacle(HikariFluidInteraction interaction, float3 p, float3 velocity, inout float3 acceleration)
{
    const float4 h = float4(p, 1.0f);
    const float3 local = float3(dot(interaction.toLocal0, h), dot(interaction.toLocal1, h), dot(interaction.toLocal2, h));
    const float mask = saturate(0.5f - HikariFluidInteractionDistance(interaction, local) / (1.5f * gCell.x));
    if (mask > 0.0f)
        acceleration = lerp(acceleration, (interaction.velocity.xyz - velocity) / gCell.z, mask);
}

// 力：t0 平流後の速度、t1 旋度 → u0（xyz 格の中心の加速度、w 温度）。u1 の平流後の密度へ注入・吸収をその場で書く。
[numthreads(4, 4, 4)]
void ForceFieldCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideCells(id))
        return;
    if (CellWork(id) != kWorkCompute)
    {
        gOut0[id] = float4(0.0f, 0.0f, 0.0f, gIn0.Load(int4(id, 0)).w);
        if (Combusting())
            gOut2[id] = float2(gIn2.Load(int4(id, 0)).x, 0.0f);
        return;
    }
    const float3 grid = float3(id) + 0.5f;
    const float3 p = DomainPosition(grid);
    const float3 velocity = CellVelocity(gIn0, id);
    float temperature = gIn0.Load(int4(id, 0)).w;
    float4 smoke = gOut1[id];
    float fuel = Combusting() ? gIn2.Load(int4(id, 0)).x : 0.0f;
    float3 acceleration = gInertia.xyz;
    for (uint index = 0u; index < gSize.w; ++index)
        ApplySource(gSources[index], p, velocity, acceleration, smoke, fuel, temperature);
    for (uint emit = 0u; emit < gInteractionCount.x; ++emit)
        if (HikariFluidInteractionKind(gInteractions[emit]) == 3u)
            ApplyEmit(gInteractions[emit], p, velocity, acceleration, smoke, temperature);
    // 燃焼は注入の後、浮力の前（燃えて上がった温度がこの子步で浮く）。
    const float burning = Burn(smoke, fuel, temperature);
    // 浮力（温度で上へ、密度で下へ）。
    acceleration += gUp.xyz * (gMotion.z * temperature - gMotion.w * smoke.x);
    // 涡度约束：旋度の大きさが増える向き N と旋度の外積で、小さな渦を保つ。
    if (gMotion.y > 0.0f)
    {
        const int3 limit = int3(gSize.xyz) - 1;
        const float3 gradient = float3(
            gIn1.Load(int4(min(int3(id) + int3(1, 0, 0), limit), 0)).w - gIn1.Load(int4(max(int3(id) - int3(1, 0, 0), 0), 0)).w,
            gIn1.Load(int4(min(int3(id) + int3(0, 1, 0), limit), 0)).w - gIn1.Load(int4(max(int3(id) - int3(0, 1, 0), 0), 0)).w,
            gIn1.Load(int4(min(int3(id) + int3(0, 0, 1), limit), 0)).w - gIn1.Load(int4(max(int3(id) - int3(0, 0, 1), 0), 0)).w);
        const float magnitude = length(gradient);
        if (magnitude > 1.0e-6f)
            acceleration += gMotion.y * gCell.x * cross(gradient / magnitude, gIn1.Load(int4(id, 0)).xyz);
    }
    for (uint push = 0u; push < gInteractionCount.x; ++push)
        if (HikariFluidIsPush(gInteractions[push]) && HikariFluidPushDirected(gInteractions[push]))
            ApplyPush(gInteractions[push], p, acceleration);
    for (uint obstacle = 0u; obstacle < gInteractionCount.x; ++obstacle)
        if (HikariFluidInteractionKind(gInteractions[obstacle]) == 0u)
            ApplyObstacle(gInteractions[obstacle], p, velocity, acceleration);
    smoke = max(smoke, 0.0f);
    ApplySinks(p, smoke, fuel, temperature);
    gOut0[id] = float4(acceleration, temperature);
    gOut1[id] = smoke;
    if (Combusting())
        gOut2[id] = float2(max(fuel, 0.0f), burning);
}

// 加速度を面へ配って速度を進める：t0 平流後の速度、t1 格の中心の加速度（w 新しい温度）→ u0。
[numthreads(4, 4, 4)]
void ApplyForcesCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
    float4 velocity = gIn0.Load(int4(id, 0));
    const int3 limit = int3(gSize.xyz) - 1;
    const int3 cell = min(int3(id), limit);
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        int3 below = int3(id);
        below[axis] -= 1;
        const float a = gIn1.Load(int4(cell, 0))[axis];
        const float b = gIn1.Load(int4(clamp(below, 0, limit), 0))[axis];
        velocity[axis] += gCell.z * 0.5f * (a + b);
    }
    velocity.w = InsideCells(id) ? gIn1.Load(int4(id, 0)).w : 0.0f;
    gOut0[id] = velocity;
}

// ---------------------------------------------------------------- 圧力
// 境界の面の速度：閉じた面の壁は 0、開いた面は解いた値のまま。
float FaceVelocity(Texture3D<float4> velocity, uint3 face, uint axis)
{
    if ((face[axis] == 0u && ClosedFace(axis, false)) || (face[axis] == gSize[axis] && ClosedFace(axis, true)))
        return 0.0f;
    return velocity.Load(int4(face, 0))[axis];
}

float Divergence(Texture3D<float4> velocity, uint3 cell)
{
    return (FaceVelocity(velocity, cell + uint3(1, 0, 0), 0u) - FaceVelocity(velocity, cell, 0u) +
            FaceVelocity(velocity, cell + uint3(0, 1, 0), 1u) - FaceVelocity(velocity, cell, 1u) +
            FaceVelocity(velocity, cell + uint3(0, 0, 1), 2u) - FaceVelocity(velocity, cell, 2u)) * gCell.y;
}

// 右辺 (D - e) h²：t0 速度 → u0（段 0 の右辺）。e は向きの無い押しの湧き出し率で、投影後の散度がそれになる。
[numthreads(4, 4, 4)]
void DivergenceCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideCells(id))
        return;
    // 全部の面が閉じた箱は湧き出しを入れない（圧力が勾配 0 だけの境界では湧き出しの総和が 0 でないと解が無い）。
    float expansion = AllClosed() ? 0.0f : Expansion(DomainPosition(float3(id) + 0.5f));
    if (!AllClosed() && Combusting())
        expansion += gCombustion1.y * gIn1.Load(int4(id, 0)).y;
    gOut0[id] = (Divergence(gIn0, id) - expansion) * gCell.x * gCell.x;
}

// 近傍の圧力の和と、数える近傍の数。閉じた壁の外は数えない（勾配 0）。開いた境界は面の上で圧力 0：外の幽霊の格を
// 内の格の符号反転とみなし、対角に 2 を足す（どの段でも零点が同じ面に来て、粗い段の修正が細かい段と食い違わない）。
void Neighbors(Texture3D<float4> pressure, RWTexture3D<float4> pressureRw, bool useRw, int3 cell, int3 size,
               out float sum, out float count)
{
    sum = 0.0f;
    count = 0.0f;
    [unroll] for (uint n = 0u; n < 6u; ++n)
    {
        int3 offset = 0;
        offset[n >> 1u] = (n & 1u) != 0u ? 1 : -1;
        const int3 neighbor = cell + offset;
        if (any(neighbor < 0) || any(neighbor >= size))
        {
            count += ClosedFace(n >> 1u, (n & 1u) != 0u) ? 0.0f : 2.0f;
            continue;
        }
        sum += useRw ? pressureRw[neighbor].x : pressure.Load(int4(neighbor, 0)).x;
        count += 1.0f;
    }
}

// 赤黒 Gauss-Seidel の半分：色 gPassParams.x の格だけ更新する。u0 圧力（その場で書く）、t0 右辺。
[numthreads(4, 4, 4)]
void SmoothCS(uint3 id : SV_DispatchThreadID)
{
    const int3 size = int3(gPassSize.xyz);
    if (any(int3(id) >= size) || ((id.x + id.y + id.z) & 1u) != gPassParams.x)
        return;
    float sum, count;
    Neighbors(gIn0, gOut0, true, int3(id), size, sum, count);
    if (count > 0.0f)
        gOut0[id] = (sum - gIn0.Load(int4(id, 0)).x) / count;
}

// 残差を粗い段へ：t0 細かい段の圧力、t1 細かい段の右辺 → u0 粗い段の右辺、u1 粗い段の圧力（0 から始める）。
// gPassSize は粗い段の大きさ、細かい段は 2 倍。
[numthreads(4, 4, 4)]
void ResidualRestrictCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= gPassSize.xyz))
        return;
    const int3 fine = int3(gPassSize.xyz) * 2;
    float residual = 0.0f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const int3 cell = int3(id) * 2 + int3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
        float sum, count;
        Neighbors(gIn0, gOut0, false, cell, fine, sum, count);
        residual += gIn1.Load(int4(cell, 0)).x - (sum - count * gIn0.Load(int4(cell, 0)).x);
    }
    gOut0[id] = 4.0f * residual / 8.0f;
    gOut1[id] = 0.0f.xxxx;
}

// 粗い段の格 c の誤差。外は Neighbors と同じ約束：開いた境界は面の上で 0（外の幽霊の格は内の格の符号反転）、閉じた壁は
// 勾配 0（内の格のまま）。
float CoarseCorrection(int3 c, int3 size)
{
    float sign = 1.0f;
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (c[axis] < 0 || c[axis] >= size[axis])
        {
            const bool upper = c[axis] >= size[axis];
            c[axis] = clamp(c[axis], 0, size[axis] - 1);
            sign = ClosedFace(axis, upper) ? sign : -sign;
        }
    }
    return sign * gIn0.Load(int4(c, 0)).x;
}

// 粗い段の誤差を細かい段へ足す：t0 粗い段の圧力 → u0 細かい段の圧力。格の中心どうしの三線形補間（重み 3/4・1/4）。
// 開いた境界の面に接する格は幽灵の格（符号反転）と補間し、面の上で修正が 0 になる。標本器の端の延長（clamp）だと
// 修正が段ごとに 2 倍になり、5 段（128³）で発散した。
[numthreads(4, 4, 4)]
void ProlongCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= gPassSize.xyz))
        return;
    const int3 coarse = int3(gPassSize.xyz) / 2;
    const float3 p = (float3(id) + 0.5f) * 0.5f - 0.5f;
    const int3 base = int3(floor(p));
    const float3 f = p - float3(base);
    float correction = 0.0f;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const int3 offset = int3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
        const float3 w = lerp(1.0f - f, f, float3(offset));
        correction += w.x * w.y * w.z * CoarseCorrection(base + offset, coarse);
    }
    gOut0[id] = gOut0[id].x + correction;
}

// 最も粗い段（格 512 個以下）を 1 つの thread group の共有メモリで解き切る。u0 圧力、t0 右辺。
groupshared float gCoarse[512];
[numthreads(8, 8, 8)]
void CoarsestSolveCS(uint3 id : SV_GroupThreadID)
{
    const int3 size = int3(gPassSize.xyz);
    const bool inside = all(int3(id) < size);
    const uint index = id.x + 8u * (id.y + 8u * id.z);
    gCoarse[index] = inside ? gOut0[id].x : 0.0f;
    const float rhs = inside ? gIn0.Load(int4(id, 0)).x : 0.0f;
    GroupMemoryBarrierWithGroupSync();
    [loop] for (uint iteration = 0u; iteration < 48u; ++iteration)
    {
        if (inside && ((id.x + id.y + id.z + iteration) & 1u) == 0u)
        {
            float sum = 0.0f;
            float count = 0.0f;
            [unroll] for (uint n = 0u; n < 6u; ++n)
            {
                int3 neighbor = int3(id);
                neighbor[n >> 1u] += (n & 1u) != 0u ? 1 : -1;
                if (any(neighbor < 0) || any(neighbor >= size))
                {
                    count += ClosedFace(n >> 1u, (n & 1u) != 0u) ? 0.0f : 2.0f;
                    continue;
                }
                sum += gCoarse[neighbor.x + 8 * (neighbor.y + 8 * neighbor.z)];
                count += 1.0f;
            }
            if (count > 0.0f)
                gCoarse[index] = (sum - rhs) / count;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (inside)
        gOut0[id] = gCoarse[index];
}

// 投影：t0 圧力（段 0）→ u0 速度（その場で書く）。閉じた境界の壁の面は 0、開いた境界の面は外の幽霊の格を
// 内の符号反転として勾配を取る（面の上で圧力 0。Neighbors と同じ約束）。
[numthreads(4, 4, 4)]
void ProjectCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
    float4 velocity = gOut0[id];
    const int3 size = int3(gSize.xyz);
    [unroll] for (uint axis = 0u; axis < 3u; ++axis)
    {
        int3 below = int3(id);
        below[axis] -= 1;
        int3 extent = size;
        extent[axis] += 1;
        if (any(int3(id) >= extent))
            continue;
        if ((id[axis] == 0u && ClosedFace(axis, false)) || (int(id[axis]) == size[axis] && ClosedFace(axis, true)))
        {
            velocity[axis] = 0.0f;
            continue;
        }
        const bool hereInside = int(id[axis]) < size[axis];
        const bool thereInside = below[axis] >= 0;
        const float hereValue = hereInside ? gIn0.Load(int4(id, 0)).x : 0.0f;
        const float thereValue = thereInside ? gIn0.Load(int4(below, 0)).x : 0.0f;
        const float here = hereInside ? hereValue : -thereValue;
        const float there = thereInside ? thereValue : -hereValue;
        velocity[axis] -= (here - there) * gCell.y;
    }
    gOut0[id] = velocity;
}

// ---------------------------------------------------------------- 占有・診断・切片
// 砖块（8³ 格）ごとの最大密度を gStats の先頭から 4 byte ずつ書く（第 7 步の疎な割り当てと描画の範囲が読む）。
groupshared float gBrickMax[512];
[numthreads(8, 8, 8)]
void OccupancyCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint flat : SV_GroupIndex)
{
    gBrickMax[flat] = InsideCells(id) ? max(gIn0.Load(int4(id, 0)).x, Combusting() ? gIn1.Load(int4(id, 0)).x : 0.0f)
                                      : 0.0f;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 256u; stride > 0u; stride >>= 1u)
    {
        if (flat < stride)
            gBrickMax[flat] = max(gBrickMax[flat], gBrickMax[flat + stride]);
        GroupMemoryBarrierWithGroupSync();
    }
    if (flat == 0u)
    {
        const uint3 bricks = (gSize.xyz + 7u) / 8u;
        gStats.Store((group.x + bricks.x * (group.y + bricks.y * group.z)) * 4u, asuint(gBrickMax[0]));
    }
}

// 砖块ごとの活動状態（子步の頭）：OccupancyCS の最大密度が烟のある砖块と、その 26 近傍を動かす。注入・吸い込み・流れを
// 入れる源（域全体の形は除く）の子步の間に掃いた形と、注入（Emit）の掃いた球に触れる砖块も動かす。フレームの最初の子步では汇の吸い込み量を 0 に戻す。
[numthreads(4, 4, 4)]
void ActivityCS(uint3 id : SV_DispatchThreadID)
{
    const uint3 bricks = BrickGrid();
    if (all(id == 0u) && (gInteractionCount.w & 1u) != 0u)
        for (uint row = 0u; row < 32u; ++row)
            gStats.Store(gInteractionCount.z + row * 4u, 0u);
    if (any(id >= bricks))
        return;
    bool active = false;
    [loop] for (int z = -1; z <= 1 && !active; ++z)
        [loop] for (int y = -1; y <= 1 && !active; ++y)
            [loop] for (int x = -1; x <= 1 && !active; ++x)
            {
                const int3 neighbor = int3(id) + int3(x, y, z);
                if (all(neighbor >= 0) && all(neighbor < int3(bricks)))
                    active = asfloat(gStats.Load(BrickIndex(uint3(neighbor)) * 4u)) > 1.0e-3f;
            }
    const float3 center = DomainPosition(float3(id) * 8.0f + 4.0f);
    const float reach = 0.5f * sqrt(3.0f) * 8.0f * gCell.x;
    for (uint index = 0u; index < gSize.w && !active; ++index)
    {
        const HikariFluidSource source = gSources[index];
        const bool feeds = source.emission.x > 0.0f || source.emission.y > 0.0f || source.flow.x != 0.0f ||
                           any(source.velocity != 0.0f);
        if (!feeds || (uint)(source.shape.x + 0.5f) == 7u)
            continue;
        // 源の局所の長さは域の長さ × 拡縮の逆（縁の柔らかさ guide.w = 1.5 格 × 拡縮の逆）。注入は表面の外 2 guide.w
        // まで届く（EmissionMask）。
        const float toSource = source.guide.w / (1.5f * gCell.x);
        active = HikariFluidSweptDistance(source, HikariFluidToSource(source, center)) <
                 reach * toSource + 2.0f * source.guide.w;
    }
    for (uint index = 0u; index < gInteractionCount.x && !active; ++index)
    {
        const HikariFluidInteraction interaction = gInteractions[index];
        active = HikariFluidInteractionKind(interaction) == 3u &&
                 length(HikariFluidFromSegment(center - interaction.push.xyz, interaction.sweep.xyz)) <
                     reach + interaction.extents.x;
    }
    gStats.Store(gInteractionCount.y + BrickIndex(id) * 4u, active ? 1u : 0u);
}

// 投影後の散度の最大と密度の総量を砖块ごとに書く（gPassParams.w からの位置、8 byte ずつ）。t0 速度、t1 密度。
groupshared float2 gBrickStats[512];
[numthreads(8, 8, 8)]
void DiagnosticsCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint flat : SV_GroupIndex)
{
    gBrickStats[flat] = InsideCells(id) ? float2(abs(Divergence(gIn0, id)), gIn1.Load(int4(id, 0)).x) : 0.0f.xx;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 256u; stride > 0u; stride >>= 1u)
    {
        if (flat < stride)
            gBrickStats[flat] = float2(max(gBrickStats[flat].x, gBrickStats[flat + stride].x),
                                       gBrickStats[flat].y + gBrickStats[flat + stride].y);
        GroupMemoryBarrierWithGroupSync();
    }
    if (flat == 0u)
    {
        const uint3 bricks = (gSize.xyz + 7u) / 8u;
        const uint brick = group.x + bricks.x * (group.y + bricks.y * group.z);
        gStats.Store2(gPassParams.w + brick * 8u, asuint(gBrickStats[0]));
    }
}

// 発光の回読（火の光源）：砖块（8³ 格）ごとに、発光量 a = 密度 × saturate(温度 - 起亮温度)（描画の HikariFluidEmission の
// 強さと同じ形）を起亮の色の分 a(1 - t) と熱い端の分 a t に分け、どちらも格の色（gPassParams.x が 1 の域だけ、描画の
// SmokeTint と同じ）を掛けて rgb で足し、a の和と a × 格子座標の和を gPassParams.w から 48 byte ずつ書く。起亮温度と熱い端の
// 温度は gPassParams.y / z（float のビット）。t0 速度（w 格の温度）、t1 密度。
groupshared float4 gBrickGlowCold[512];
groupshared float3 gBrickGlowHot[512];
groupshared float3 gBrickGlowCenter[512];
[numthreads(8, 8, 8)]
void GlowCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint flat : SV_GroupIndex)
{
    float amount = 0.0f;
    float share = 0.0f;
    float3 tint = 1.0f.xxx;
    if (InsideCells(id))
    {
        const float ignition = asfloat(gPassParams.y);
        const float span = asfloat(gPassParams.z) - ignition;
        const float temperature = gIn0.Load(int4(id, 0)).w;
        const float4 smoke = gIn1.Load(int4(id, 0));
        amount = max(smoke.x, 0.0f) * saturate(temperature - ignition);
        share = span > 1.0e-4f ? saturate((temperature - ignition) / span) : 0.0f;
        if (gPassParams.x != 0u)
            tint = saturate(smoke.yzw / max(smoke.x, 1.0e-4f));
    }
    gBrickGlowCold[flat] = float4(amount * (1.0f - share) * tint, amount);
    gBrickGlowHot[flat] = amount * share * tint;
    gBrickGlowCenter[flat] = amount * (float3(id) + 0.5f);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 256u; stride > 0u; stride >>= 1u)
    {
        if (flat < stride)
        {
            gBrickGlowCold[flat] += gBrickGlowCold[flat + stride];
            gBrickGlowHot[flat] += gBrickGlowHot[flat + stride];
            gBrickGlowCenter[flat] += gBrickGlowCenter[flat + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (flat == 0u)
    {
        const uint3 bricks = (gSize.xyz + 7u) / 8u;
        const uint address = gPassParams.w + (group.x + bricks.x * (group.y + bricks.y * group.z)) * 48u;
        gStats.Store4(address, asuint(gBrickGlowCold[0]));
        gStats.Store4(address + 16u, asuint(float4(gBrickGlowHot[0], 0.0f)));
        gStats.Store4(address + 32u, asuint(float4(gBrickGlowCenter[0], 0.0f)));
    }
}

// 調試の切片（2D）：軸 gPassParams.z に垂直な、格 gPassParams.w の面。量 gPassParams.y：0 密度、1 速度、2 散度、3 圧力。
// t0 速度、t1 密度、t2 圧力（段 0）→ u4。
[numthreads(8, 8, 1)]
void SliceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gPassSize.xy))
        return;
    const uint axis = gPassParams.z;
    uint3 cell = axis == 0u ? uint3(gPassParams.w, id.x, id.y) : axis == 1u ? uint3(id.x, gPassParams.w, id.y)
                                                                          : uint3(id.x, id.y, gPassParams.w);
    cell = min(cell, gSize.xyz - 1u);
    float4 value = 0.0f.xxxx;
    const bool visualize = gPassParams.x != 0u;
    if (gPassParams.y == 0u)
    {
        const float density = gIn1.Load(int4(cell, 0)).x;
        value = visualize ? float4((1.0f - exp(-density)).xxx, 1.0f) : float4(density.xxx, 1.0f);
    }
    else if (gPassParams.y == 1u)
    {
        const float3 velocity = CellVelocity(gIn0, cell);
        value = visualize ? float4(saturate(0.5f + velocity * 0.125f), 1.0f) : float4(velocity, length(velocity));
    }
    else
    {
        // 散度 [1/s] は ±2 で、圧力（格の大きさ² × 散度の単位）は ±0.05 で振り切る。
        const float raw = gPassParams.y == 2u ? Divergence(gIn0, cell) : gIn2.Load(int4(cell, 0)).x;
        const float scaled = raw * (gPassParams.y == 2u ? 0.5f : 20.0f);
        value = visualize ? float4(saturate(scaled), 0.08f, saturate(-scaled), 1.0f) : float4(raw, 0.0f, 0.0f, 1.0f);
    }
    gSlice[id.xy] = value;
}
