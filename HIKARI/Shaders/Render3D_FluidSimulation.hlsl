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
    float4 gMotion;       // x 境界（0 開、1 閉）、y 涡度约束、z 浮力（温度 1 あたり）、w 重さ（密度 1 あたり）
    float4 gDissipation;  // x 密度の消散 [1/s]、y 温度の消散 [1/s]
    float4 gUp;           // xyz 域の局所の上向き
    float4 gInertia;      // xyz 挂点の加速度の逆向き（域の局所 [m/s²]）
};

cbuffer FluidPassCB : register(b1)
{
    uint4 gPassSize;      // xyz この pass が回す格子（段の格の数、切片の画素数）
    uint4 gPassParams;    // x 赤黒の色、y 切片の量、z 切片の軸、w 切片の位置（格）
};

Texture3D<float4> gIn0 : register(t0);
Texture3D<float4> gIn1 : register(t1);
Texture3D<float4> gIn2 : register(t2);
Texture3D<float4> gIn3 : register(t3);
StructuredBuffer<HikariFluidSource> gSources : register(t6);
RWTexture3D<float4> gOut0 : register(u0);
RWTexture3D<float4> gOut1 : register(u1);
RWTexture2D<float4> gSlice : register(u4);
RWByteAddressBuffer gStats : register(u5);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearBorder : register(s1);

bool Closed() { return gMotion.x > 0.5f; }

// 格の中心の密度を格子座標 p で読む。閉じた箱は壁の外を壁の値で延ばし（量を失わない）、開いた境界は外を 0 にする
// （外から戻ってくる烟は無い）。
float DensityAt(Texture3D<float4> density, float3 p)
{
    if (Closed())
        return HikariFluidCellAt(density, gLinearClamp, p, float3(gSize.xyz)).x;
    return HikariFluidCellAt(density, gLinearBorder, p, float3(gSize.xyz)).x;
}
float3 Cells() { return float3(gSize.xyz); }
float3 VelocityTexels() { return float3(gSize.xyz + 1u); }
bool InsideCells(uint3 cell) { return all(cell < gSize.xyz); }
bool InsideFaces(uint3 face) { return all(face <= gSize.xyz); }

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
// 前進：速度 t0（面 + 温度）と密度 t1 を速度で後ろへたどって読み、u0 / u1 へ書く。
[numthreads(4, 4, 4)]
void AdvectForwardCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
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
    if (InsideCells(id))
    {
        const float3 p = float3(id) + 0.5f;
        const float3 back = p - step * VelocityAt(gIn0, p);
        velocity.w = gIn0.SampleLevel(gLinearClamp, back / VelocityTexels(), 0).w;
        gOut1[id] = float4(DensityAt(gIn1, back), 0.0f, 0.0f, 0.0f);
    }
    gOut0[id] = velocity;
}

// 修正：t0 元の速度、t1 元の密度、t2 前進の速度、t3 前進の密度。前進の結果を進む向きにたどり直した値との差の半分で
// 誤差を打ち消し、後ろへたどった位置の元の 8 標本の範囲を外れたら前進の結果へ戻す。消散もここで掛ける。
[numthreads(4, 4, 4)]
void AdvectCorrectCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideFaces(id))
        return;
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
    if (InsideCells(id))
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
        const float densityForward = gIn3.Load(int4(id, 0)).x;
        float density = densityForward + 0.5f * (gIn1.Load(int4(id, 0)).x - DensityAt(gIn3, ahead));
        CellRange(gIn1, 0u, back, int3(gSize.xyz) - 1, low, high);
        density = density < low || density > high ? densityForward : density;
        gOut1[id] = float4(max(density, 0.0f) * exp(-gDissipation.x * gCell.z), 0.0f, 0.0f, 0.0f);
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

// 源 1 つの加速度と注入（格の中心、域の局所 p）。velocity は今の速度。
void ApplySource(HikariFluidSource source, float3 p, float3 velocity, inout float3 acceleration,
                 inout float density, inout float temperature)
{
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
    // 形状引导：形の表面から帯の厚さの中の烟を表面へ引き戻す。
    if (source.guide.x > 0.0f)
    {
        const float distance = HikariFluidSourceDistance(source, local);
        const float thickness = max(source.guide.y, 1.0e-3f);
        if (abs(distance) < thickness)
        {
            const float e = 0.25f * thickness;
            const float3 gradient = float3(
                HikariFluidSourceDistance(source, local + float3(e, 0, 0)) - HikariFluidSourceDistance(source, local - float3(e, 0, 0)),
                HikariFluidSourceDistance(source, local + float3(0, e, 0)) - HikariFluidSourceDistance(source, local - float3(0, e, 0)),
                HikariFluidSourceDistance(source, local + float3(0, 0, e)) - HikariFluidSourceDistance(source, local - float3(0, 0, e)));
            const float3 normal = HikariFluidToDomain(source, gradient / max(length(gradient), 1.0e-6f));
            acceleration -= normal * (distance * source.guide.x * (1.0f - abs(distance) / thickness));
        }
    }
    if (mask <= 0.0f)
        return;
    // 注入する速度（源の局所の速度と放射方向）へ寄せる。
    const float3 injected = HikariFluidToDomain(source, source.velocity.xyz + source.velocity.w * normalize(local + 1.0e-6f));
    if (any(source.velocity != 0.0f))
        acceleration += (injected - velocity) * (8.0f * mask);
    const float added = mask * (source.emission.x * gCell.z + source.emission.y);
    if (added > 0.0f)
    {
        temperature += (source.emission.z - temperature) * saturate(mask * 8.0f * gCell.z + mask * saturate(source.emission.y));
        density += added;
    }
    density *= exp(-source.emission.w * mask * gCell.z);
}

// 力：t0 平流後の速度、t1 旋度 → u0（xyz 格の中心の加速度、w 温度）。u1 の平流後の密度へ注入・吸収をその場で書く。
[numthreads(4, 4, 4)]
void ForceFieldCS(uint3 id : SV_DispatchThreadID)
{
    if (!InsideCells(id))
        return;
    const float3 grid = float3(id) + 0.5f;
    const float3 p = DomainPosition(grid);
    const float3 velocity = CellVelocity(gIn0, id);
    float temperature = gIn0.Load(int4(id, 0)).w;
    float density = gOut1[id].x;
    float3 acceleration = gInertia.xyz;
    for (uint index = 0u; index < gSize.w; ++index)
        ApplySource(gSources[index], p, velocity, acceleration, density, temperature);
    // 浮力（温度で上へ、密度で下へ）。
    acceleration += gUp.xyz * (gMotion.z * temperature - gMotion.w * density);
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
    gOut0[id] = float4(acceleration, temperature);
    gOut1[id] = float4(max(density, 0.0f), 0.0f, 0.0f, 0.0f);
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
// 境界の面の速度：閉じた境界の壁は 0、開いた境界は解いた値のまま。
float FaceVelocity(Texture3D<float4> velocity, uint3 face, uint axis)
{
    if (Closed() && (face[axis] == 0u || face[axis] == gSize[axis]))
        return 0.0f;
    return velocity.Load(int4(face, 0))[axis];
}

float Divergence(Texture3D<float4> velocity, uint3 cell)
{
    return (FaceVelocity(velocity, cell + uint3(1, 0, 0), 0u) - FaceVelocity(velocity, cell, 0u) +
            FaceVelocity(velocity, cell + uint3(0, 1, 0), 1u) - FaceVelocity(velocity, cell, 1u) +
            FaceVelocity(velocity, cell + uint3(0, 0, 1), 2u) - FaceVelocity(velocity, cell, 2u)) * gCell.y;
}

// 右辺 D h²：t0 速度 → u0（段 0 の右辺）。
[numthreads(4, 4, 4)]
void DivergenceCS(uint3 id : SV_DispatchThreadID)
{
    if (InsideCells(id))
        gOut0[id] = Divergence(gIn0, id) * gCell.x * gCell.x;
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
            count += Closed() ? 0.0f : 2.0f;
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

// 粗い段の誤差を細かい段へ足す：t0 粗い段の圧力 → u0 細かい段の圧力。
[numthreads(4, 4, 4)]
void ProlongCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= gPassSize.xyz))
        return;
    const float correction = gIn0.SampleLevel(gLinearClamp, (float3(id) + 0.5f) / float3(gPassSize.xyz), 0).x;
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
                    count += Closed() ? 0.0f : 2.0f;
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
        const bool wall = id[axis] == 0u || int(id[axis]) == size[axis];
        if (Closed() && wall)
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
    gBrickMax[flat] = InsideCells(id) ? gIn0.Load(int4(id, 0)).x : 0.0f;
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
