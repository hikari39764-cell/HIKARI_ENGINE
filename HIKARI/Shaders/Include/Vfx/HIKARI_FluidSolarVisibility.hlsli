#ifndef HIKARI_FLUID_SOLAR_VISIBILITY_INCLUDED
#define HIKARI_FLUID_SOLAR_VISIBILITY_INCLUDED

// 流体の烟を場面へ落とす影の表（Render3D_FluidVolume.hlsl の SolarVisibilityCS）。域の表 gDomains・密度 gDensity0〜3・
// gSolarTable・gSolarAtlas と DomainRho は読み込む側の宣言を使う。

// 全ての域の消光 [1/m]（細部の模様なし）。gDomains には影を落とす域だけが詰めてある。
float DomainsExtinction(float3 world)
{
    const uint count = DomainCount();
    float sigma = 0.0f;
    if (count > 0u)
        sigma += DomainRho(gDomains[0], gDensity0, HikariFluidToGrid(gDomains[0], world)) * gDomains[0].look.w;
    if (count > 1u)
        sigma += DomainRho(gDomains[1], gDensity1, HikariFluidToGrid(gDomains[1], world)) * gDomains[1].look.w;
    if (count > 2u)
        sigma += DomainRho(gDomains[2], gDensity2, HikariFluidToGrid(gDomains[2], world)) * gDomains[2].look.w;
    if (count > 3u)
        sigma += DomainRho(gDomains[3], gDensity3, HikariFluidToGrid(gDomains[3], world)) * gDomains[3].look.w;
    return sigma;
}

// 烟の影の表（雲の影の表と同じ並び）。列 (x, y) は太陽に垂直な面の格子、tile0 は列が箱を通る区間（太陽向きの距離
// near, far。通らない列は 0, 0）、tile1..steps は太陽側から等分した深さまでの累積透過率（1 段 4 標本）。
[numthreads(8, 8, 1)]
void SolarVisibilityCS(uint3 id : SV_DispatchThreadID)
{
    const uint resolution = (uint)(gSolarTable.upResolution.w + 0.5f);
    const uint steps = (uint)(gSolarTable.directionSteps.w + 0.5f);
    if (any(id.xy >= resolution.xx))
        return;
    const float2 planar = ((float2(id.xy) + 0.5f) / (float)resolution - 0.5f) * (2.0f * gSolarTable.rightExtent.w);
    const float3 base = gSolarTable.originDescriptor.xyz + gSolarTable.rightExtent.xyz * planar.x +
        gSolarTable.upResolution.xyz * planar.y;
    const float3 toSun = gSolarTable.directionSteps.xyz;
    float2 interval = float2(1.0e30f, -1.0e30f);
    [unroll] for (uint index = 0u; index < HIKARI_FLUID_MAX_DOMAINS; ++index)
    {
        if (index >= DomainCount())
            break;
        const float3 o = HikariFluidToGrid(gDomains[index], base);
        const float2 hit =
            HikariFluidBoxInterval(o, HikariFluidToGrid(gDomains[index], base + toSun) - o, gDomains[index].cells.xyz);
        if (hit.x <= hit.y)
            interval = float2(min(interval.x, hit.x), max(interval.y, hit.y));
    }
    const bool crossed = interval.x < interval.y;
    gSolarAtlas[HikariCloudSolarAtlasPixel(id.xy, 0u, resolution)] = float4(crossed ? interval : 0.0f.xx, 0.0f, 0.0f);
    if (!crossed)
        return;
    const float slice = (interval.y - interval.x) / (float)steps;
    float optical = 0.0f;
    [loop] for (uint depth = 1u; depth <= steps; ++depth)
    {
        [unroll] for (uint sample = 0u; sample < 4u; ++sample)
            optical += DomainsExtinction(base + toSun * (interval.y - ((float)(depth - 1u) + ((float)sample + 0.5f) * 0.25f) * slice));
        gSolarAtlas[HikariCloudSolarAtlasPixel(id.xy, depth, resolution)] = float4(exp(-optical * 0.25f * slice), 0.0f, 0.0f, 0.0f);
    }
}

#endif
