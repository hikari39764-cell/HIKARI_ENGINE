// 体積流体（VFX の流体層）の描画。重なった域は 1 本の視線で一緒に積分する。
// 計算：NoiseCS（細部の 3D ノイズ）、LightCS（域ごとの光照体積：太陽と空への光学的な厚さ）、CameraVolumeCS（透明物体の
// 相機体積）、SolarVisibilityCS（場面へ落とす烟の影の表。雲の影の表と同じ並び）。
// 全画面：MarchPS（半分辨率で步进、場面の深度で止める）→ CompositePS（深度を見て全解像度へ広げ、ONE / INV_SRC_ALPHA で
// 場面へ重ねる：C·T + S）→ CoveragePS（烟の平均距離と不透明度を全解像度で書き、体積霧の合成へ渡す）。時間方向の履歴は
// 持たない（抖动は画素に固定してあり、静止した相機では毎フレーム同じ絵になる）。
#include "Include/Lighting/HIKARI_CloudSolarVisibility.hlsli"
#include "Include/Lighting/HIKARI_ShadowCascadeContract.hlsli"
#include "Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "Include/Sky/HIKARI_VolumetricCloudNoise.hlsli"
#include "Include/Vfx/HIKARI_FluidGrid.hlsli"
#include "Include/Vfx/HIKARI_FluidMedium.hlsli"

#define HIKARI_FLUID_MAX_DOMAINS 4

cbuffer FluidRenderCB : register(b0)
{
    float4x4 gInvRelativeViewProj; // 相機を原点に置いた逆 view-projection（clip → 相機からの相対位置）
    float4 gCamera;          // xyz 相機の位置、w 步进の歩の上限 [格]（画質）
    float4 gForward;         // xyz 相機の前、w 步进の步数
    float4 gSize;            // xy 半分辨率の大きさ、zw 描画の大きさ
    float4 gSun;             // xyz 太陽の向き（世界）、w 1 なら級联阴影を使う
    float4 gSunRadiance;     // rgb 太陽の照度（前露光済み）
    float4 gAmbient;         // rgb 環境光の輝度（前露光済み）、w 域の数
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
Texture3D<float> gNoise : register(t15);
Texture2D<float> gShadowMap : register(t16);
// 域ごとの解算の統計の緩衝：砖块（8³ 格）の数 × 12 byte の位置から砖块ごとの活動状態（u32、1 で動いている。烟のある砖块
// とその 26 近傍、注入の届く砖块。HIKARI_FluidSolver.h の FluidDomainResources::ActivityOffset）。
ByteAddressBuffer gActivity0 : register(t17);
ByteAddressBuffer gActivity1 : register(t18);
ByteAddressBuffer gActivity2 : register(t19);
ByteAddressBuffer gActivity3 : register(t20);
RWTexture3D<float4> gOutput : register(u0);
RWTexture2D<float4> gSolarAtlas : register(u1);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearBorder : register(s1);
SamplerState gLinearWrap : register(s2);
SamplerState gPointClamp : register(s3);
SamplerComparisonState gShadowSampler : register(s4);

uint DomainCount() { return (uint)(gAmbient.w + 0.5f); }

// 域の格子座標 grid での烟（x 密度、yzw 密度 × 色。開いた境界の縁で消える分を含む。箱の外は 0）。
float4 DomainSmoke(HikariFluidDomain domain, Texture3D<float4> density, float3 grid)
{
    if (!HikariFluidInside(grid, domain.cells.xyz))
        return 0.0f.xxxx;
    return density.SampleLevel(gLinearBorder, grid / domain.cells.xyz, 0) * HikariFluidEdgeFade(domain, grid);
}

// 烟の色（格ごとの色の域は 密度 × 色 / 密度、それ以外は白）。
float3 SmokeTint(HikariFluidDomain domain, float4 smoke)
{
    return domain.sun.w > 0.5f ? saturate(smoke.yzw / max(smoke.x, 1.0e-4f)) : 1.0f.xxx;
}

// 解算は固定の子步で進む。最後の子步から貯まった時間 detail.w の分だけ流れに沿って遡って密度を読み、子步の無いフレームで
// 烟が止まり次のフレームで跳ぶ（フレームの速さが子步の速さと違う時の一顿一顿）のを無くす。速度は空いた所にもあるので、
// 遡った後で空かどうかを決める（先に元の位置で空と決めると、空いた所へ進む烟の前縁が外推されない）。描画の步进と
// 烟の影の表が同じこの関数で読む。grid は遡った後の格子座標になり、cell は元の位置の格の中心の標本（w 温度、xyz 下側の
// 面の速度 [m/s]）。
float4 ExtrapolatedSmoke(HikariFluidDomain domain, Texture3D<float4> density, Texture3D<float4> velocity,
    inout float3 grid, out float4 cell)
{
    cell = velocity.SampleLevel(gLinearClamp, grid / (domain.cells.xyz + 1.0f), 0);
    grid -= cell.xyz * (domain.detail.w / domain.cells.w);
    return DomainSmoke(domain, density, grid);
}

// ---------------------------------------------------------------- 視線（体積雾と同じ再構成）
// 相機を原点にして組み立ててから相機の位置を足す（世界の逆行列を float で使うと、浮島の高さ y≈3400 m では向きが
// 相機の位置につれて数画素ずつ跳び、歩くと烟だけが場面に対して一顿一顿と揺れた）。
void ViewRay(float2 uv, out float3 origin, out float3 direction)
{
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInvRelativeViewProj, 0.0f.xxx, origin, direction, orthographic);
    origin += gCamera.xyz;
}

// 場面の深度（device）を視線の距離へ。空は大きな値。
float SceneDistance(float2 uv, float depth, float3 origin, float3 direction)
{
    if (depth >= 0.999999f)
        return 1.0e7f;
    return dot(HikariUnprojectVolumePosition(uv, depth, gInvRelativeViewProj) - (origin - gCamera.xyz), direction);
}

// ---------------------------------------------------------------- 介質
// 場面の物が太陽を遮る割合（級联阴影の届く範囲だけ。外は遮らない）。步进の標本ごとに読み、光筋・影筋の縁を
// 光照体積の格の大きさに縛らない（標本ごとの抖动で縁がぼける）。
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

// 細部を足した密度：平均を変えずに模様で揺らす。薄い所（細部の模様 1 つで光学的な厚さが 1 に届かない密度
// 1 / (消光 × detailScale) より下）ほど大きく揺らして綿のように崩し、濃い芯は 3 割だけ揺らす。体積雲のように引いて削ると
// 大きな場地の烟（ほとんど「薄い」側）の光学的な厚さが半分になり、厚い球壳の向こうが透けた。pixelSize は画素の大きさ [m]
// （負なら細部なし）。ノイズの 1 texel が画素より細かいと模様が揺らぎキャッシュも外れるので、その距離で読まない。
float DetailedDensity(HikariFluidDomain domain, float rho, float3 flow, float3 grid, float pixelSize)
{
    if (pixelSize < 0.0f)
        return rho;
    const float strength = domain.optics.w * saturate(2.0f - pixelSize * domain.detail.x * 64.0f);
    if (strength <= 0.0f)
        return rho;
    // 細部の模様を流れに沿って運ぶ（位相を半周期ずらした 2 枚を交互に溶かす）。周期が長いと流れの剪断で模様が
    // 筋に引き伸ばされるので 1 秒にとどめる。流れは格の速度の近似（温度と同じ 1 回の標本）で足りる。時間は最後の子步からの
    // 端数（detail.w）も足して、子步の無いフレームでも模様が止まらないようにする。
    const float3 local = grid * domain.cells.w;
    const float period = 1.0f;
    const float time = domain.detail.y + domain.detail.w;
    const float phase0 = frac(time / period);
    const float phase1 = frac(time / period + 0.5f);
    const float weight = abs(phase0 * 2.0f - 1.0f);
    // 1 周期のずれは細部の模様の大きさ（detailScale = 1 / (4 detail.x)）までに抑える。速い流れ（大きな場地の環流で
    // 5〜10 m/s）でずれが模様より長いと、模様が流れの向きに筋として伸びる。
    const float3 drift = flow * period;
    const float driftLength = length(drift);
    const float3 carried = drift * min(1.0f, 0.25f / max(domain.detail.x * driftLength, 1.0e-6f));
    const float n0 = gNoise.SampleLevel(gLinearWrap, (local - carried * phase0) * domain.detail.x, 0);
    const float n1 = gNoise.SampleLevel(gLinearWrap, (local - carried * phase1) * domain.detail.x + 0.37f, 0);
    const float noise = lerp(n0, n1, weight);
    const float thin = 4.0f * domain.detail.x / max(domain.look.w, 1.0e-4f);
    const float edge = saturate(1.0f - rho / thin);
    const float wobble = smoothstep(0.15f, 0.85f, noise) * 2.0f - 1.0f;
    // 4 倍細かい模様は薄い縁だけを削る：格の大きさの塊（三線形補間の折れ目）が輪郭に出るのを崩し、外から見た縁を締める。
    // ずれはその模様の大きさまで、画素より細かくなる距離では読まない（粗い模様と同じ約束）。
    const float fineFrequency = domain.detail.x * 4.0f;
    const float fineStrength = saturate(2.0f - pixelSize * fineFrequency * 64.0f) * edge;
    float fine = 0.0f;
    if (fineStrength > 0.0f)
    {
        const float3 fineCarried = drift * min(1.0f, 0.25f / max(fineFrequency * driftLength, 1.0e-6f));
        const float f0 = gNoise.SampleLevel(gLinearWrap, (local - fineCarried * phase0) * fineFrequency + 0.21f, 0);
        const float f1 = gNoise.SampleLevel(gLinearWrap, (local - fineCarried * phase1) * fineFrequency + 0.58f, 0);
        fine = fineStrength * (smoothstep(0.2f, 0.8f, lerp(f0, f1, weight)) * 2.0f - 1.0f);
    }
    return rho * max(1.0f + strength * (wobble * lerp(0.3f, 1.25f, edge) + 0.6f * fine), 0.0f);
}

// 太陽への光学的な厚さ：手前 kNearSunCells 格は格の解像度の密度で積み、その先は光照体積（格 2 つの texel、そこから
// 太陽までの厚さ）を読む。光照体積だけだと烟の表面の明暗が texel の塊になり、大きな域を外から見ると階段状に見えた。
static const uint kNearSunCells = 3u;
float SunOpticalDepth(HikariFluidDomain domain, Texture3D<float4> density, Texture3D<float4> light, float3 grid)
{
    float nearDensity = 0.0f;
    [unroll] for (uint index = 0u; index < kNearSunCells; ++index)
        nearDensity += density.SampleLevel(gLinearBorder, (grid + domain.sun.xyz * ((float)index + 0.5f)) /
                                                              domain.cells.xyz, 0).x;
    const float3 ahead = grid + domain.sun.xyz * (float)kNearSunCells;
    const float far = HikariFluidInside(ahead, domain.cells.xyz)
                          ? light.SampleLevel(gLinearClamp, ahead / domain.cells.xyz, 0).x
                          : 0.0f;
    return nearDensity * domain.look.w * domain.cells.w + far;
}

// 世界の点 world の消光 sigma と視線へ出る光（sigma × 散乱 + 発光）を足す。pixelSize は DetailedDensity へ。
void AccumulateDomain(HikariFluidDomain domain, Texture3D<float4> density, Texture3D<float4> velocity,
    Texture3D<float4> light, float3 world, float3 viewDirection, float pixelSize, inout float sigma,
    inout float3 emitted)
{
    float3 grid = HikariFluidToGrid(domain, world);
    // cell の xyz は細部を運ぶ流れの近似、w は発光の温度にも使う。
    float4 cell;
    const float4 smoke = ExtrapolatedSmoke(domain, density, velocity, grid, cell);
    float rho = smoke.x;
    if (rho <= 1.0e-4f)
        return;
    const float3 uvw = grid / domain.cells.xyz;
    rho = DetailedDensity(domain, rho, cell.xyz, grid, pixelSize);
    if (rho <= 0.0f)
        return;
    const float extinction = rho * domain.look.w;
    // 光照体積は光学的な厚さ（透過率で補間すると厚い烟で隣と桁が違い、折れ目が格子の塊として陰影に出る）。y は空、z は
    // 近くの遮り。太陽は手前を格の解像度で積む（SunOpticalDepth）のは、光照体積の texel（格 2 つ）が画面で 2 画素以上ある
    // 時（それより細かければ texel の塊は見えない）で、光照体積だけで太陽への厚さが 4 を越えない時（影の奥は手前を細かく
    // 積んでも明るさが変わらない）だけ。近い烟（KnightBoss）の標本ごとに密度 3 回の読みが増え、描画が倍になった。
    const float3 tau = light.SampleLevel(gLinearClamp, uvw, 0).xyz;
    const bool nearSun = pixelSize > 0.0f && 2.0f * domain.cells.w >= 2.0f * pixelSize && tau.x < 4.0f;
    const float2 transmittance =
        exp(-float2(nearSun ? SunOpticalDepth(domain, density, light, grid) : tau.x, tau.y));
    const float openness = exp(-0.5f * tau.z);
    const float cosTheta = dot(viewDirection, normalize(gSun.xyz));
    const float3 sun = gSunRadiance.rgb * HikariFluidSunScattering(cosTheta, domain.optics.x, domain.optics.y,
                                                                   transmittance.x * SceneSunVisibility(world), openness);
    const float3 ambient = gAmbient.rgb * (domain.optics.z * HikariFluidSkyVisibility(transmittance.y, domain.optics.y,
                                                                                       domain.optics.x, openness));
    const float temperature = cell.w;
    // 格ごとの色は散乱色と発光の両方に掛ける（同じ域で赤と青の発光する烟が混ざらずに並ぶ）。
    const float3 tint = SmokeTint(domain, smoke);
    sigma += extinction;
    emitted += tint * (extinction * domain.look.rgb * (sun + ambient) + rho * HikariFluidEmission(domain, temperature));
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

// 視線が各域の箱を通る区間（距離、相機の後ろは 0 から）と、その域だけを描く時の歩の長さ（箱の区間 / 步数）。
// 成分が域の番号。通らない域と空いた枠は enter = 1e30、leave = -1e30。
struct DomainSpans
{
    float4 enter;
    float4 leave;
    float4 step;
};

DomainSpans TraceDomains(float3 origin, float3 direction, float2 footprint)
{
    DomainSpans spans;
    spans.enter = 1.0e30f.xxxx;
    spans.leave = -1.0e30f.xxxx;
    spans.step = 1.0e30f.xxxx;
    const float steps = max(gForward.w, 1.0f);
    [unroll] for (uint index = 0u; index < HIKARI_FLUID_MAX_DOMAINS; ++index)
    {
        if (index >= DomainCount())
            break;
        const HikariFluidDomain domain = gDomains[index];
        const float3 o = HikariFluidToGrid(domain, origin);
        const float3 d = HikariFluidToGrid(domain, origin + direction) - o;
        const float2 interval = HikariFluidBoxInterval(o, d, domain.cells.xyz);
        const float enter = max(interval.x, 0.0f);
        if (enter < interval.y)
        {
            spans.enter[index] = enter;
            spans.leave[index] = interval.y;
            // 歩は「箱の区間 / 步数」と「格の大きさ × 画質の倍率（gCamera.w）」の短い方（区間で割るだけだと 60 m の場地の烟で
            // 1 m 近い歩になり、1〜2 m の厚さの地面の霧を 1〜2 標本しか取れず、抖动の粒がそのまま見えた）。ただし入口の画素の
            // 大きさより短くはしない（格が画素より細かく映る遠い域は画素ごとに 1 標本で足り、歩数を減らす）。近くの細かな
            // 域（KnightBoss の 2 cm の格）を 0.8 格で歩くと区間 / 步数の 2.5 倍の標本になり、描画が倍以上になった。
            // 動いていない砖块は飛ばす（Integrate）。
            const float span = interval.y - enter;
            const float pixel = footprint.x < 0.0f ? 0.0f : footprint.x + footprint.y * enter;
            spans.step[index] = min(span / steps, max(domain.cells.w * gCamera.w, pixel));
        }
    }
    return spans;
}

// どれかの域を通る最初と最後の距離（どれも通らなければ x > y）。
float2 SpansBounds(DomainSpans spans)
{
    return float2(min(min(spans.enter.x, spans.enter.y), min(spans.enter.z, spans.enter.w)),
                  max(max(spans.leave.x, spans.leave.y), max(spans.leave.z, spans.leave.w)));
}

// 步进の累積：散乱（前露光済み）と透過率、烟の距離を失った光の割合で重み付けした和と重み（体積霧の合成で烟の位置に使う）。
struct MarchState
{
    float3 scattered;
    float transmittance;
    float smokeDistance;
    float smokeWeight;
};

MarchState BeginMarch()
{
    MarchState march;
    march.scattered = 0.0f.xxx;
    march.transmittance = 1.0f;
    march.smokeDistance = 0.0f;
    march.smokeWeight = 0.0f;
    return march;
}

bool BrickActive(uint domainIndex, uint3 brick, uint3 bricks)
{
    const uint address = (bricks.x * bricks.y * bricks.z * 3u + brick.x + bricks.x * (brick.y + bricks.y * brick.z)) * 4u;
    [branch] switch (domainIndex)
    {
    case 0u: return (gActivity0.Load(address) & 1u) != 0u;
    case 1u: return (gActivity1.Load(address) & 1u) != 0u;
    case 2u: return (gActivity2.Load(address) & 1u) != 0u;
    default: return (gActivity3.Load(address) & 1u) != 0u;
    }
}

// 距離 t の点が、入っているどの域でも動いていない砖块の中なら、その砖块を全ての域で出る距離（end まで）を返す。どれかの域の
// 動いている砖块の中なら t（そこは步进する）。动いていない砖块は烟も届く注入も無いので、标本を取らず飛ばしてよい。
float SkipInactive(float3 origin, float3 direction, DomainSpans spans, float t, float end)
{
    float exit = end;
    [unroll] for (uint index = 0u; index < HIKARI_FLUID_MAX_DOMAINS; ++index)
    {
        if (index >= DomainCount() || t < spans.enter[index] || t >= spans.leave[index])
            continue;
        const HikariFluidDomain domain = gDomains[index];
        const float3 o = HikariFluidToGrid(domain, origin);
        const float3 d = HikariFluidToGrid(domain, origin + direction) - o;
        const float3 grid = o + d * t;
        const uint3 bricks = ((uint3)domain.cells.xyz + 7u) / 8u;
        const uint3 brick = min(uint3(max(grid, 0.0f) / 8.0f), bricks - 1u);
        if (BrickActive(index, brick, bricks))
            return t;
        const float3 inverse = 1.0f / lerp(1.0e-7f.xxx, d, (float3)(abs(d) > 1.0e-7f));
        const float3 low = (float3(brick) * 8.0f - grid) * inverse;
        const float3 high = ((float3(brick) + 1.0f) * 8.0f - grid) * inverse;
        const float3 leave = max(low, high);
        exit = min(exit, t + max(min(min(leave.x, leave.y), leave.z), 0.0f) + 1.0e-3f);
    }
    return exit;
}

// [start, end] を stepLength [m] ごとに解析的に積分して march へ続ける（最後の歩は end で切る）。どの域でも動いていない
// 砖块は飛ばす。footprint は距離 t の画素の大きさ x + y t（x < 0 で細部なし）。歩と飛ばしを合わせて 512 回まで。
void Integrate(float3 origin, float3 direction, DomainSpans spans, float start, float end, float stepLength,
    float jitter, float2 footprint, inout MarchState march)
{
    stepLength = max(stepLength, 1.0e-4f);
    float stepStart = start;
    [loop] for (uint iteration = 0u; iteration < 512u && stepStart < end; ++iteration)
    {
        const float skipped = SkipInactive(origin, direction, spans, stepStart, end);
        if (skipped > stepStart)
        {
            stepStart = skipped;
            continue;
        }
        const float span = min(stepLength, end - stepStart);
        stepStart += span;
        const float t = stepStart - span + jitter * span;
        float sigma;
        float3 emitted;
        Medium(origin + direction * t, direction, footprint.x < 0.0f ? -1.0f : footprint.x + footprint.y * t, sigma,
            emitted);
        if (sigma <= 0.0f)
            continue;
        const float segment = exp(-sigma * span);
        const float lost = march.transmittance * (1.0f - segment);
        march.scattered += emitted / sigma * lost;
        march.smokeDistance += lost * t;
        march.smokeWeight += lost;
        march.transmittance *= segment;
        // 打ち切る時は残りを「先も同じ明るさの烟」として散乱に回し 0 にする（1% 残すと暗い球壳の内で空が透ける）。
        if (march.transmittance < 0.01f)
        {
            march.scattered += emitted / sigma * march.transmittance;
            march.smokeDistance += march.transmittance * t;
            march.smokeWeight += march.transmittance;
            march.transmittance = 0.0f;
            break;
        }
    }
}

// [start, end] を域の箱の境目で区切り、区切りごとに中にある域の最も短い歩（minimumStep より短くはしない）で積分する
// （どの域も無い間は飛ばす）。小さな域は大きな域と同じ視線に乗っても自分だけの時と同じ歩で標本を取り、大きな域の歩に
// 引き伸ばされない。区切りの歩数はそこを通る域の歩数の和を超えないので、1 本の標本は域の数 × 步数まで。境目は域の数 × 2 まで。
void MarchDomains(float3 origin, float3 direction, DomainSpans spans, float start, float end, float minimumStep,
    float jitter, float2 footprint, inout MarchState march)
{
    float t = start;
    [loop] for (uint piece = 0u; piece < 2u * HIKARI_FLUID_MAX_DOMAINS; ++piece)
    {
        if (t >= end || march.transmittance <= 0.0f)
            break;
        float next = 1.0e30f;
        float stepLength = 1.0e30f;
        [unroll] for (uint index = 0u; index < HIKARI_FLUID_MAX_DOMAINS; ++index)
        {
            if (spans.enter[index] > t)
                next = min(next, spans.enter[index]);
            else if (spans.leave[index] > t)
            {
                next = min(next, spans.leave[index]);
                stepLength = min(stepLength, spans.step[index]);
            }
        }
        if (next >= 1.0e30f)
            break;
        if (stepLength < 1.0e30f)
            Integrate(origin, direction, spans, t, min(next, end), max(stepLength, minimumStep), jitter, footprint,
                march);
        t = next;
    }
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

// 域 gPass.x の光照体積（格子の半分の分辨率）：x 太陽への光学的な厚さ（自己影）、y 空への光学的な厚さ（上の円錐と水平
// 4 方向の透過率の平均を厚さへ戻したもの）、z 近くの遮り（6 方向へ約 8 格の光学的な厚さの平均）。
static const uint kSkyRingTexels = 48u;
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
    // 太陽（点の光源）は 1 本の光線。
    {
        const float3 step = domain.sun.xyz * stride;
        float3 p = start + step * 0.5f;
        [loop] for (uint index = 0u; index < 256u && HikariFluidInside(p, cells); ++index)
        {
            optical.x += gDensity0.SampleLevel(gLinearBorder, p / cells, 0).x * stride;
            p += step;
        }
    }
    // 空の光は上へ広がる円錐（半角約 35°）の 4 点で積分する（1 本だと濃い塊が真下へ柱の影を落とし縦の筋が出る）。
    {
        const float3 side0 = normalize(cross(up, abs(up.x) < 0.9f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f)));
        const float3 side1 = cross(up, side0);
        [loop] for (uint index = 0u; index < 256u; ++index)
        {
            const float travelled = ((float)index + 0.5f) * stride;
            const float3 p = start + up * travelled;
            if (!HikariFluidInside(p, cells))
                break;
            const float spread = 0.7f * travelled;
            const float sum = gDensity0.SampleLevel(gLinearBorder, (p + side0 * spread) / cells, 0).x +
                              gDensity0.SampleLevel(gLinearBorder, (p - side0 * spread) / cells, 0).x +
                              gDensity0.SampleLevel(gLinearBorder, (p + side1 * spread) / cells, 0).x +
                              gDensity0.SampleLevel(gLinearBorder, (p - side1 * spread) / cells, 0).x;
            optical.y += 0.25f * sum * stride;
        }
    }
    // 空の光は上だけでなく地平の周りからも来る：大きな烟の塊の横腹（上は厚い烟でも横へ開いている）が上の円錐だけでは
    // 真っ黒になった。水平 4 方向へ 1 本ずつ kSkyRingTexels まで積み（遠くの烟は環境光をほとんど遮らない）、上の円錐の
    // 透過率と半々で平均する。
    const float scale = domain.look.w * domain.cells.w;
    float ring = 0.0f;
    {
        const float3 side0 = normalize(cross(up, abs(up.x) < 0.9f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f)));
        const float3 side1 = cross(up, side0);
        [unroll] for (uint side = 0u; side < 4u; ++side)
        {
            const float3 direction = ((side & 1u) != 0u ? -1.0f : 1.0f) * (side < 2u ? side0 : side1);
            float along = 0.0f;
            [loop] for (uint index = 0u; index < kSkyRingTexels; ++index)
            {
                const float3 p = start + direction * (((float)index + 0.5f) * stride);
                if (!HikariFluidInside(p, cells))
                    break;
                along += gDensity0.SampleLevel(gLinearBorder, p / cells, 0).x * stride;
            }
            ring += exp(-along * scale);
        }
    }
    const float sky = -log(max(0.5f * exp(-optical.y * scale) + 0.125f * ring, 1.0e-6f));
    // 近くの遮り：多重散乱の光は周りから来るので、塊の外へ開いた所（凸）は明るく、奥や褶の中は暗い。大きな烟の影の側と
    // 球壳の内側（太陽への厚さがどこも同じくらいで、それだけだと一様な灰色）に陰影の起伏を付ける。
    float local = 0.0f;
    [unroll] for (uint axis = 0u; axis < 6u; ++axis)
    {
        float3 direction = 0.0f.xxx;
        direction[axis >> 1u] = (axis & 1u) != 0u ? -1.0f : 1.0f;
        [unroll] for (uint index = 1u; index <= 4u; ++index)
            local += gDensity0.SampleLevel(gLinearBorder, (start + direction * (stride * (float)index)) / cells, 0).x;
    }
    gOutput[id] = float4(optical.x * scale, sky, local * (stride / 6.0f) * scale, 0.0f);
}

// ---------------------------------------------------------------- 場面へ落とす烟の影
#include "Include/Vfx/HIKARI_FluidSolarVisibility.hlsli"

// 透明物体のための相機体積：画面の (x, y) ごとに near から奥へ層を進み、相機からその層までの累積（rgb 散乱、a 透過率）。
// 層の中の域がある所だけを、層の域の範囲にかかる長さ / 各層の標本数より細かくない歩で積分する（粗い体積なので步进ほど
// 細かく取らない）。
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
    const DomainSpans spans = TraceDomains(origin, direction, float2(-1.0f, 0.0f));
    const float2 bounds = SpansBounds(spans);
    MarchState march = BeginMarch();
    float previous = gCameraVolume.x;
    const float samples = max(gCameraVolume.w, 1.0f);
    for (uint slice = 0u; slice < slices; ++slice)
    {
        const float distance = gCameraVolume.x * exp(((float)slice + 0.5f) / (float)slices * gCameraVolume.y);
        const float covered = min(distance, bounds.y) - max(previous, bounds.x);
        if (covered > 0.0f)
            MarchDomains(origin, direction, spans, previous, distance, covered / samples, 0.5f, float2(-1.0f, 0.0f),
                march);
        gOutput[uint3(id.xy, slice)] = float4(march.scattered, march.transmittance);
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
    float2 depth : SV_TARGET1;  // x 止めた不透明面の距離（合成で全解像度の深度と比べる）、y 烟の平均距離（烟が無ければ x）
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
    // 隣の画素の視線との差で画素の大きさ（直交投影は起点の差、透視は向きの差 × 距離）。
    float3 nextOrigin, nextDirection;
    ViewRay(uv + float2(0.0f, 1.0f / gSize.y), nextOrigin, nextDirection);
    const float2 footprint = float2(length(nextOrigin - origin), length(nextDirection - direction));
    const DomainSpans spans = TraceDomains(origin, direction, footprint);
    const float2 bounds = SpansBounds(spans);
    output.depth = opaque.xx;
    output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (min(bounds.y, opaque) <= bounds.x)
        return output;
    // 画素ごとに散らした抖动（交错勾配ノイズ）。
    const float noise = frac(52.9829189f * frac(dot(float2(halfPixel), float2(0.06711056f, 0.00583715f))));
    // 歩の長さは域ごとの箱の区間全体で決め、物の手前では止めるだけ（止める所で割ると物の輪郭が烟に浮き出る）。
    MarchState march = BeginMarch();
    // 抖动は画素に固定する（フレームごとに回すと粒が毎フレーム入れ替わり、烟全体がざらついて揺れた。時間方向の履歴で
    // 均すと、相機が動く間に輪郭へ残像が出て烟全体が跳ねた。步数が十分なので固定の交错勾配ノイズの模様は目立たない）。
    MarchDomains(origin, direction, spans, bounds.x, opaque, 0.0f, noise, footprint, march);
    output.color = float4(march.scattered, march.transmittance);
    output.depth.y = march.smokeWeight > 1.0e-4f ? march.smokeDistance / march.smokeWeight : opaque;
    return output;
}

// 深度を見て半分辨率の步进を全解像度の画素 position へ広げる（rgb 散乱、a 透過率）。smoke は烟の平均距離 [m]：隣の不透明度で
// 重みを付け、烟の無い隣（距離は背景面、空なら 1e7）を混ぜない。烟が無ければ不透明面の距離。覆いの RG16F に収まるよう
// 6e4 m で抑える（霧の合成は烟の距離が不透明面より手前の時だけ使う）。
float4 UpsampleMarch(float2 uv, float2 position, out float smoke)
{
    float3 origin, direction;
    ViewRay(uv, origin, direction);
    const float opaque = SceneDistance(uv, gSceneDepth.Load(int3(int2(position), 0)), origin, direction);
    const float2 halfCoordinate = uv * gSize.xy - 0.5f;
    const int2 base = int2(floor(halfCoordinate));
    const float2 fraction = halfCoordinate - float2(base);
    float4 sum = 0.0f.xxxx;
    float smokeSum = 0.0f;
    float smokeWeight = 0.0f;
    float weightSum = 0.0f;
    [unroll] for (uint corner = 0u; corner < 4u; ++corner)
    {
        const int2 offset = int2(corner & 1u, corner >> 1u);
        const int2 texel = clamp(base + offset, 0, int2(gSize.xy) - 1);
        const float bilinear = (offset.x != 0 ? fraction.x : 1.0f - fraction.x) *
                               (offset.y != 0 ? fraction.y : 1.0f - fraction.y);
        const float2 distances = gInputDepth.Load(int3(texel, 0));
        const float relative = abs(distances.x - opaque) / max(min(distances.x, opaque), 0.1f);
        const float weight = bilinear * exp(-relative * 16.0f) + 1.0e-5f;
        const float4 color = gInputColor.Load(int3(texel, 0));
        sum += color * weight;
        const float covered = weight * (1.0f - color.a);
        smokeSum += distances.y * covered;
        smokeWeight += covered;
        weightSum += weight;
    }
    smoke = min(smokeWeight > 1.0e-6f ? smokeSum / smokeWeight : opaque, 6.0e4f);
    return sum / weightSum;
}

// 乗算済みで場面へ重ねる（混合 ONE / INV_SRC_ALPHA で C·T + S）。
float4 CompositePS(FullscreenOutput input) : SV_TARGET0
{
    float smoke;
    const float4 fluid = UpsampleMarch(input.uv, input.position.xy, smoke);
    return float4(fluid.rgb, 1.0f - fluid.a);
}

// 体積霧の合成へ渡す烟の覆い（全解像度）：x 烟の平均距離 [m]、y 不透明度。霧は合成の時にこの距離までの霧と不透明面までの
// 霧を不透明度で混ぜる（烟に隠れた物の輪郭が霧で浮き出ない。Render3D_VolumetricLighting.hlsl の CompositeCS）。
float2 CoveragePS(FullscreenOutput input) : SV_TARGET0
{
    float smoke;
    const float4 fluid = UpsampleMarch(input.uv, input.position.xy, smoke);
    return float2(smoke, 1.0f - fluid.a);
}
