#ifndef HIKARI_FLUID_SIMULATION_STATS
#define HIKARI_FLUID_SIMULATION_STATS
// Render3D_FluidSimulation.hlsl の統計と調べの入口（占有・砖块の活動状態・診断・発光の回読・切片）。解算の束ねと
// 補助（BrickGrid、Divergence、源と押すものの表など）は読み込む側が先に用意する。

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
                    active = asfloat(gStats.Load(BrickIndex(uint3(neighbor)) * 4u)) > gFrameWind.w;
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
    // 粒子からの注入を受け取る子步：注入の印のある砖块とその 26 近傍を動かす。
    if (InjectionReceiving())
        [loop] for (int z = -1; z <= 1 && !active; ++z)
            [loop] for (int y = -1; y <= 1 && !active; ++y)
                [loop] for (int x = -1; x <= 1 && !active; ++x)
                {
                    const int3 neighbor = int3(id) + int3(x, y, z);
                    if (all(neighbor >= 0) && all(neighbor < int3(bricks)))
                        active = InjectionMarked(uint3(neighbor));
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

// 粒子からの注入の緩衝を 0 に戻す（gPassSize.x 語）。
[numthreads(64, 1, 1)]
void InjectionClearCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x < gPassSize.x)
        gInjection.Store(id.x * 4u, 0u);
}

#endif
