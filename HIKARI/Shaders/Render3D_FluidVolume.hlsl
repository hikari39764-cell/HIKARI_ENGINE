// 体積流体（VFX の流体層）の描画。重なった域は 1 本の視線で一緒に積分する。
// 計算：NoiseCS（細部の 3D ノイズを一度だけ作る）、LightCS（域ごとの光照体積：太陽へ向かう自己影、空への透過率。場面の
// 級联阴影は步进の標本ごと）、
// CameraVolumeCS（透明物体のための低分辨率の相機体積）、SolarVisibilityCS（場面へ落とす烟の影の表。雲の影の表と同じ並び）。
// 全画面：MarchPS（半分辨率で步进、場面の深度で止める）→ ReconstructPS（相機の動きで履歴を再投影して時間方向に混ぜる）
// → CompositePS（深度を見て全解像度へ広げ、ONE / INV_SRC_ALPHA で場面へ重ねる：C·T + S）。
#include "Include/Lighting/HIKARI_CloudSolarVisibility.hlsli"
#include "Include/Lighting/HIKARI_ShadowCascadeContract.hlsli"
#include "Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "Include/Sky/HIKARI_VolumetricCloudNoise.hlsli"
#include "Include/Vfx/HIKARI_FluidGrid.hlsli"
#include "Include/Vfx/HIKARI_FluidMedium.hlsli"

#define HIKARI_FLUID_MAX_DOMAINS 4

cbuffer FluidRenderCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float4 gCamera;          // xyz 相機の位置
    float4 gForward;         // xyz 相機の前、w 未使用
    float4 gSize;            // xy 半分辨率の大きさ、zw 描画の大きさ
    float4 gSun;             // xyz 太陽の向き（世界）、w 1 なら級联阴影を使う
    float4 gSunRadiance;     // rgb 太陽の照度（前露光済み）
    float4 gAmbient;         // rgb 環境光の輝度（前露光済み）、w 前フレームからの露出の比
    float4 gFrame;           // x 抖动の位相 [0,1)、y 履歴の重み（0 で履歴なし）、z 步进の步数、w 域の数
    float4 gCameraVolume;    // x near [m]、y log(far/near)、z 層の数、w 各層の標本数
    float4x4 gLightViewProj[4];
    float4 gShadowSplits;
    float4 gShadowParams;    // x 級联数、y 混合、zw 図集の大きさの割合
    float4 gShadowSampling;  // x 深度の偏り、yz 図集の texel
    float4 gShadowBias;
    HikariCloudSolarVisibilityData gSolarTable; // SolarVisibilityCS が書く烟の影の表の列と深さ（他の pass は読まない）
    HikariFluidDomain gDomains[HIKARI_FLUID_MAX_DOMAINS];
};

cbuffer FluidPassCB : register(b1)
{
    uint4 gPass;             // x 域の番号（LightCS）、yzw 未使用
};

Texture3D<float4> gDensity0 : register(t0);
Texture3D<float4> gDensity1 : register(t1);
Texture3D<float4> gDensity2 : register(t2);
Texture3D<float4> gDensity3 : register(t3);
Texture3D<float4> gVelocity0 : register(t4);
Texture3D<float4> gVelocity1 : register(t5);
Texture3D<float4> gVelocity2 : register(t6);
Texture3D<float4> gVelocity3 : register(t7);
Texture3D<float4> gLight0 : register(t8);
Texture3D<float4> gLight1 : register(t9);
Texture3D<float4> gLight2 : register(t10);
Texture3D<float4> gLight3 : register(t11);
Texture2D<float> gSceneDepth : register(t12);
Texture2D<float4> gInputColor : register(t13);
Texture2D<float2> gInputDepth : register(t14);
Texture2D<float4> gHistoryColor : register(t15);
Texture3D<float> gNoise : register(t16);
Texture2D<float> gShadowMap : register(t17);
RWTexture3D<float4> gOutput : register(u0);
RWTexture2D<float4> gSolarAtlas : register(u1);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearBorder : register(s1);
SamplerState gLinearWrap : register(s2);
SamplerState gPointClamp : register(s3);
SamplerComparisonState gShadowSampler : register(s4);

uint DomainCount() { return (uint)(gFrame.w + 0.5f); }

// 域の格子座標 grid での密度（開いた境界の縁で消える分を含む。箱の外は 0）。
float DomainRho(HikariFluidDomain domain, Texture3D<float4> density, float3 grid)
{
    if (!HikariFluidInside(grid, domain.cells.xyz))
        return 0.0f;
    return density.SampleLevel(gLinearBorder, grid / domain.cells.xyz, 0).x * HikariFluidEdgeFade(domain, grid);
}

// ---------------------------------------------------------------- 視線（体積雾と同じ再構成）
void ViewRay(float2 uv, out float3 origin, out float3 direction)
{
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInvViewProj, gCamera.xyz, origin, direction, orthographic);
}

// 場面の深度（device）を視線の距離へ。空は大きな値。
float SceneDistance(float2 uv, float depth, float3 origin, float3 direction)
{
    if (depth >= 0.999999f)
        return 1.0e7f;
    return dot(HikariUnprojectVolumePosition(uv, depth, gInvViewProj) - origin, direction);
}

// ---------------------------------------------------------------- 介質
// 場面の物が太陽を遮る割合（級联阴影の届く範囲だけ。外は遮らない）。步进の標本ごとに読み、光筋・影筋の縁を
// 光照体積の格の大きさに縛らない（抖动と時間の再構成で滑らかになる）。
float SceneSunVisibility(float3 world)
{
    if (gSun.w < 0.5f)
        return 1.0f;
    const float viewDepth = dot(world - gCamera.xyz, gForward.xyz);
    const uint count = clamp((uint)round(gShadowParams.x), 1u, 4u);
    if (viewDepth > gShadowSplits[count - 1u])
        return 1.0f;
    const uint cascade = HikariSelectShadowCascade(viewDepth, gShadowSplits, count);
    bool valid;
    return HikariSampleVolumeShadowCascade(gShadowMap, gShadowSampler, gLightViewProj[cascade], world, cascade,
        gShadowParams.zw, gShadowSampling.yz, gShadowSampling.x * gShadowBias[cascade], valid);
}

// 細部の密度の倍率。pixelSize はこの点での画素の大きさ [m]（負なら細部なし）。ノイズの 1 texel が画素より細かいと
// 模様が欠けて揺らぎ、読む位置も散ってキャッシュが外れるので、画素が 1〜2 texel になる距離で消して読まない。
float SampleDetail(HikariFluidDomain domain, float3 flow, float3 grid, float pixelSize)
{
    if (pixelSize < 0.0f)
        return 1.0f;
    const float strength = domain.optics.w * saturate(2.0f - pixelSize * domain.detail.x * 64.0f);
    if (strength <= 0.0f)
        return 1.0f;
    // 細部の模様を流れに沿って運ぶ（位相を半周期ずらした 2 枚を交互に溶かす）。周期が長いと流れの剪断で模様が
    // 筋に引き伸ばされるので 1 秒にとどめる。流れは格の速度の近似（温度と同じ 1 回の標本）で足りる。
    const float3 local = grid * domain.cells.w;
    const float period = 1.0f;
    const float phase0 = frac(domain.detail.y / period);
    const float phase1 = frac(domain.detail.y / period + 0.5f);
    const float weight = abs(phase0 * 2.0f - 1.0f);
    // 1 周期のずれは細部の模様の大きさ（detailScale = 1 / (4 detail.x)）までに抑える。速い流れ（大きな場地の環流で
    // 5〜10 m/s）でずれが模様より長いと、模様が流れの向きに筋として伸びる。
    const float3 drift = flow * period;
    const float driftLength = length(drift);
    const float3 carried = drift * min(1.0f, 0.25f / max(domain.detail.x * driftLength, 1.0e-6f));
    const float n0 = gNoise.SampleLevel(gLinearWrap, (local - carried * phase0) * domain.detail.x, 0);
    const float n1 = gNoise.SampleLevel(gLinearWrap, (local - carried * phase1) * domain.detail.x + 0.37f, 0);
    const float noise = lerp(n0, n1, weight);
    return saturate(1.0f + strength * (noise * 2.0f - 1.0f));
}

// 世界の点 world での消光 sigma と、そこから視線へ出る光（sigma を掛けた散乱 + 発光）を足す。pixelSize は細部の
// 距離による消え方（SampleDetail）。
void AccumulateDomain(HikariFluidDomain domain, Texture3D<float4> density, Texture3D<float4> velocity,
    Texture3D<float4> light, float3 world, float3 viewDirection, float pixelSize, inout float sigma,
    inout float3 emitted)
{
    const float3 grid = HikariFluidToGrid(domain, world);
    float rho = DomainRho(domain, density, grid);
    if (rho <= 1.0e-4f)
        return;
    const float3 uvw = grid / domain.cells.xyz;
    // 格の中心の標本：w は温度、xyz は下側の面の速度（細部を運ぶ流れの近似）。
    const float4 cell = velocity.SampleLevel(gLinearClamp, grid / (domain.cells.xyz + 1.0f), 0);
    rho *= SampleDetail(domain, cell.xyz, grid, pixelSize);
    const float extinction = rho * domain.look.w;
    const float2 transmittance = light.SampleLevel(gLinearClamp, uvw, 0).xy;
    const float cosTheta = dot(viewDirection, normalize(gSun.xyz));
    const float3 sun = gSunRadiance.rgb *
        HikariFluidSunScattering(cosTheta, domain.optics.x, domain.optics.y, transmittance.x * SceneSunVisibility(world));
    const float3 ambient = gAmbient.rgb * (domain.optics.z * HikariFluidSkyVisibility(transmittance.y, domain.optics.y));
    const float temperature = cell.w;
    sigma += extinction;
    emitted += extinction * domain.look.rgb * (sun + ambient) + rho * HikariFluidEmission(domain, temperature);
}

void Medium(float3 world, float3 viewDirection, float pixelSize, out float sigma, out float3 emitted)
{
    sigma = 0.0f;
    emitted = 0.0f.xxx;
    const uint count = DomainCount();
    if (count > 0u)
        AccumulateDomain(gDomains[0], gDensity0, gVelocity0, gLight0, world, viewDirection, pixelSize, sigma, emitted);
    if (count > 1u)
        AccumulateDomain(gDomains[1], gDensity1, gVelocity1, gLight1, world, viewDirection, pixelSize, sigma, emitted);
    if (count > 2u)
        AccumulateDomain(gDomains[2], gDensity2, gVelocity2, gLight2, world, viewDirection, pixelSize, sigma, emitted);
    if (count > 3u)
        AccumulateDomain(gDomains[3], gDensity3, gVelocity3, gLight3, world, viewDirection, pixelSize, sigma, emitted);
}

// 全ての域の箱を視線が通る区間（距離）。どれも通らなければ x > y。
float2 DomainsInterval(float3 origin, float3 direction)
{
    float2 result = float2(1.0e30f, -1.0e30f);
    [unroll] for (uint index = 0u; index < HIKARI_FLUID_MAX_DOMAINS; ++index)
    {
        if (index >= DomainCount())
            break;
        const HikariFluidDomain domain = gDomains[index];
        const float3 o = HikariFluidToGrid(domain, origin);
        const float3 d = HikariFluidToGrid(domain, origin + direction) - o;
        const float2 interval = HikariFluidBoxInterval(o, d, domain.cells.xyz);
        if (interval.x <= interval.y)
            result = float2(min(result.x, interval.x), max(result.y, interval.y));
    }
    return float2(max(result.x, 0.0f), result.y);
}

// 区間 [start, end] を steps 歩で積分する（区間内で一定の消光として解析的に）。footprint は距離 t での画素の大きさ
// footprint.x + footprint.y * t [m]（x が負なら細部なし）。
float4 Integrate(float3 origin, float3 direction, float start, float end, uint steps, float jitter, float2 footprint,
    out float meanDistance)
{
    float3 scattered = 0.0f.xxx;
    float transmittance = 1.0f;
    float weightedDistance = 0.0f;
    float absorbed = 0.0f;
    const float stepLength = (end - start) / max((float)steps, 1.0f);
    [loop] for (uint index = 0u; index < steps; ++index)
    {
        const float t = start + ((float)index + jitter) * stepLength;
        float sigma;
        float3 emitted;
        Medium(origin + direction * t, direction, footprint.x < 0.0f ? -1.0f : footprint.x + footprint.y * t, sigma,
            emitted);
        if (sigma <= 0.0f)
            continue;
        const float segment = exp(-sigma * stepLength);
        const float lost = transmittance * (1.0f - segment);
        scattered += emitted / sigma * lost;
        weightedDistance += t * lost;
        absorbed += lost;
        transmittance *= segment;
        if (transmittance < 0.01f)
            break;
    }
    meanDistance = absorbed > 1.0e-4f ? weightedDistance / absorbed : 1.0e7f;
    return float4(scattered, transmittance);
}

// ---------------------------------------------------------------- 計算
// 細部の 3D ノイズ（64³、周期でつながる Perlin-Worley。体積雲と同じ関数）。
[numthreads(4, 4, 4)]
void NoiseCS(uint3 id : SV_DispatchThreadID)
{
    const float3 uv = (float3(id) + 0.5f) / 64.0f;
    const float perlin = HikariCloudPerlinFbm3D(uv, 4, 0x2545f491u);
    const float worley = 1.0f - HikariCloudWorleyFbm3D(uv, 4, 0x9e3779b9u);
    gOutput[id] = saturate(perlin * 0.55f + worley * 0.45f).xxxx;
}

// 域 gPass.x の光照体積（格子の半分の分辨率）：x 太陽への透過率（自己影）、y 空（上）への透過率。
[numthreads(4, 4, 4)]
void LightCS(uint3 id : SV_DispatchThreadID)
{
    uint width, height, depth;
    gOutput.GetDimensions(width, height, depth);
    if (any(id >= uint3(width, height, depth)))
        return;
    const HikariFluidDomain domain = gDomains[gPass.x];
    const float3 cells = domain.cells.xyz;
    const float3 start = (float3(id) + 0.5f) * cells / float3(width, height, depth);
    const float3 up = normalize(HikariFluidToGrid(domain, float3(0.0f, 1.0f, 0.0f)) -
                                HikariFluidToGrid(domain, float3(0.0f, 0.0f, 0.0f)));
    // 光照体積の texel と同じ幅（格 2 つ）で進む。optical は格を単位にした密度の積分。
    const float stride = cells.x / (float)width;
    float2 optical = 0.0f.xx;
    [unroll] for (uint direction = 0u; direction < 2u; ++direction)
    {
        const float3 step = (direction == 0u ? domain.sun.xyz : up) * stride;
        float3 p = start + step * 0.5f;
        [loop] for (uint index = 0u; index < 256u && HikariFluidInside(p, cells); ++index)
        {
            optical[direction] += gDensity0.SampleLevel(gLinearBorder, p / cells, 0).x * stride;
            p += step;
        }
    }
    const float2 transmittance = exp(-optical * domain.look.w * domain.cells.w);
    gOutput[id] = float4(transmittance, 0.0f, 0.0f);
}

// ---------------------------------------------------------------- 場面へ落とす烟の影
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

// 烟の影の表（雲の影の表と同じ並びで HikariSampleCloudSolarVisibility が読む）。列 (x, y) は太陽に垂直な面の格子で、
// tile0 は列が域の箱を通る区間（太陽の向きの符号付き距離 near, far。通らない列は 0, 0 で読む側が 1 を返す）、
// tile1..steps は far（太陽側）から near へ等分した深さまでの累積透過率。1 段を 4 標本で積分する。
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

// 透明物体のための相機体積：画面の (x, y) ごとに near から奥へ層を進み、相機からその層までの累積（rgb 散乱、a 透過率）。
[numthreads(8, 8, 1)]
void CameraVolumeCS(uint3 id : SV_DispatchThreadID)
{
    uint width, height, slices;
    gOutput.GetDimensions(width, height, slices);
    if (any(id.xy >= uint2(width, height)))
        return;
    const float2 uv = (float2(id.xy) + 0.5f) / float2(width, height);
    float3 origin, direction;
    ViewRay(uv, origin, direction);
    const float2 interval = DomainsInterval(origin, direction);
    float3 scattered = 0.0f.xxx;
    float transmittance = 1.0f;
    float previous = gCameraVolume.x;
    const uint samples = max((uint)(gCameraVolume.w + 0.5f), 1u);
    for (uint slice = 0u; slice < slices; ++slice)
    {
        const float distance = gCameraVolume.x * exp(((float)slice + 0.5f) / (float)slices * gCameraVolume.y);
        const float start = max(previous, interval.x);
        const float end = min(distance, interval.y);
        if (end > start)
        {
            float mean;
            const float4 part = Integrate(origin, direction, start, end, samples, 0.5f, float2(-1.0f, 0.0f), mean);
            scattered += transmittance * part.rgb;
            transmittance *= part.a;
        }
        gOutput[uint3(id.xy, slice)] = float4(scattered, transmittance);
        previous = distance;
    }
}

// ---------------------------------------------------------------- 全画面
struct FullscreenOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

FullscreenOutput FullscreenVS(uint vertex : SV_VertexID)
{
    FullscreenOutput output;
    output.uv = float2((vertex << 1u) & 2u, vertex & 2u);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

struct MarchOutput
{
    float4 color : SV_TARGET0;  // rgb 散乱（前露光済み）、a 透過率
    float2 depth : SV_TARGET1;  // x 流体の代表距離、y 止めた不透明面の距離
};

// 半分辨率の画素ごとに、全解像度の 2x2 の最も近い不透明面まで步进する。
MarchOutput MarchPS(FullscreenOutput input)
{
    MarchOutput output;
    const uint2 halfPixel = uint2(input.position.xy);
    const float2 uv = (float2(halfPixel) + 0.5f) / gSize.xy;
    float3 origin, direction;
    ViewRay(uv, origin, direction);
    const uint2 full = min(halfPixel * 2u, uint2(gSize.zw) - 2u);
    const float depth = min(min(gSceneDepth.Load(int3(full, 0)), gSceneDepth.Load(int3(full + uint2(1, 0), 0))),
                            min(gSceneDepth.Load(int3(full + uint2(0, 1), 0)), gSceneDepth.Load(int3(full + uint2(1, 1), 0))));
    const float opaque = SceneDistance(uv, depth, origin, direction);
    const float2 interval = DomainsInterval(origin, direction);
    const float end = min(interval.y, opaque);
    output.depth = float2(1.0e7f, opaque);
    output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (end <= interval.x)
        return output;
    // 画素ごとに散らした抖动（交错勾配ノイズ）をフレームの位相で回す。
    const float noise = frac(52.9829189f * frac(dot(float2(halfPixel), float2(0.06711056f, 0.00583715f))));
    // 隣の画素の視線との差で画素の大きさ（直交投影は起点の差、透視は向きの差 × 距離）。
    float3 nextOrigin, nextDirection;
    ViewRay(uv + float2(0.0f, 1.0f / gSize.y), nextOrigin, nextDirection);
    const float2 footprint = float2(length(nextOrigin - origin), length(nextDirection - direction));
    float mean;
    output.color = Integrate(origin, direction, interval.x, end, (uint)(gFrame.z + 0.5f), frac(noise + gFrame.x),
        footprint, mean);
    output.depth.x = mean;
    return output;
}

// 前フレームの履歴を流体の代表距離で再投影し、今の 3x3 の範囲に収めて混ぜる。
float4 ReconstructPS(FullscreenOutput input) : SV_TARGET0
{
    const int2 pixel = int2(input.position.xy);
    const float4 current = gInputColor.Load(int3(pixel, 0));
    if (gFrame.y <= 0.0f)
        return current;
    float4 low = current;
    float4 high = current;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float4 neighbor = gInputColor.Load(int3(clamp(pixel + int2(x, y), 0, int2(gSize.xy) - 1), 0));
            low = min(low, neighbor);
            high = max(high, neighbor);
        }
    const float2 uv = (float2(pixel) + 0.5f) / gSize.xy;
    float3 origin, direction;
    ViewRay(uv, origin, direction);
    const float distance = gInputDepth.Load(int3(pixel, 0)).x;
    const float3 world = origin + direction * min(distance, 1.0e5f);
    const float4 previous = mul(gPrevViewProj, float4(world, 1.0f));
    const float2 previousUv = float2(previous.x / previous.w * 0.5f + 0.5f, 0.5f - previous.y / previous.w * 0.5f);
    if (previous.w <= 0.0f || any(previousUv < 0.0f) || any(previousUv > 1.0f))
        return current;
    float4 history = gHistoryColor.SampleLevel(gLinearClamp, previousUv, 0);
    history.rgb *= gAmbient.w;
    history = clamp(history, low, high);
    return lerp(current, history, gFrame.y);
}

// 深度を見て半分辨率から全解像度へ広げ、乗算済みで場面へ重ねる（混合 ONE / INV_SRC_ALPHA で C·T + S）。
float4 CompositePS(FullscreenOutput input) : SV_TARGET0
{
    const float2 uv = input.uv;
    float3 origin, direction;
    ViewRay(uv, origin, direction);
    const float opaque = SceneDistance(uv, gSceneDepth.Load(int3(int2(input.position.xy), 0)), origin, direction);
    const float2 halfCoordinate = uv * gSize.xy - 0.5f;
    const int2 base = int2(floor(halfCoordinate));
    const float2 fraction = halfCoordinate - float2(base);
    float4 sum = 0.0f.xxxx;
    float weightSum = 0.0f;
    [unroll] for (uint corner = 0u; corner < 4u; ++corner)
    {
        const int2 offset = int2(corner & 1u, corner >> 1u);
        const int2 texel = clamp(base + offset, 0, int2(gSize.xy) - 1);
        const float bilinear = (offset.x != 0 ? fraction.x : 1.0f - fraction.x) *
                               (offset.y != 0 ? fraction.y : 1.0f - fraction.y);
        const float stopped = gInputDepth.Load(int3(texel, 0)).y;
        const float relative = abs(stopped - opaque) / max(min(stopped, opaque), 0.1f);
        const float weight = bilinear * exp(-relative * 16.0f) + 1.0e-5f;
        sum += gInputColor.Load(int3(texel, 0)) * weight;
        weightSum += weight;
    }
    const float4 fluid = sum / weightSum;
    return float4(fluid.rgb, 1.0f - fluid.a);
}
