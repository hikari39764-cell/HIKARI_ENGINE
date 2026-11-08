#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"
#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"

#include <cmath>
#include <string>
#include <utility>

// 烟でできた弧形の剑气（远程の斬撃）。FluidSmokeShowcase の Sample_Crescent_Qi / Sample_Crescent_Qi_Hit で実拍して決めた。
// 剑气が読めるのは 形・锋・势・迹・收：細い月牙が自分の刃面の中を凸の側から飛び、刃口が最も濃い一筋の烟、刃身の烟は刃口の
// 後ろから流れて丝に裂け、二つの刃尖が翼尖の渦を引き、射程の端で当たって爆ぜる。
// 層：
//   Trail Domain：世界空間の流体域。飛ぶ道と当たった所を覆い、刃尖の尾迹と爆ぜた烟が残る。
//   Crescent（組）：刃面（組の XY）を飛行軸の周りに倒して斜めに斬る。等速で飛び、刃口の中央が射程に着いたら止まる。
//     Blade Domain：刃に付いて飛ぶ局所空間の域（格約 3 cm）。相対風が刃の烟を後ろへ流す。
//       Blade Edge / Blade Body：刃口の薄く濃い弧と、刃身中段の厚い弧（流体源の円環の弧）。
//       Impact Push：当たった瞬間に刃の烟を前へ押す（刃面の中で潰れて広がる）。
//     Tip Upper / Tip Lower：刃尖の源（両方の域へ入る）。飛行軸の周りの旋渦で翼尖の渦を巻く。
//   Impact Burst：当たった点で 0.08 s、外向きの速さを付けた烟を入れる（打撃の爆ぜ）。湧き出し（outflow）は形の中で一様に
//     膨らんで滑らかな風船の球になるので使わない。
//   Impact Churn：当たった点の周りを 0.6 s 強い卷曲乱流でかき回し、爆ぜた烟を丝に裂く。
// 旋钮：smoke（刃と刃尖の烟の量）、glow（刃口の白い光）、turbulence（烟の乱れ）、impact（爆ぜの強さ）。
namespace HIKARI::VFX::PRESETS::INTERNAL {
namespace {
constexpr float kRadius = 3.0f;   // 刃口（前縁）の半径 [m]
constexpr float kHalfArc = 78.0f; // 刃尖の角度 [deg]（刃は ±kHalfArc）
constexpr float kLaunch = 0.1f;   // 出刃 [s]
constexpr float kRange = 22.0f;   // 刃口の中央が当たる距離 [m]
constexpr float kSpeed = 28.0f;   // [m/s]
constexpr float kHold = 1.4f;     // 当たった後に組を残す時間 [s]
constexpr float kRoll = 50.0f;    // 刃面を飛行軸の周りに倒す角 [deg]（0 は縦斬り、90 は横斬り）
constexpr float kHeight = 1.3f;   // 刃の中心の高さ [m]
constexpr float kScaleStart = 0.85f;
constexpr float kScaleHit = 1.05f; // 飛ぶ間に刃が少し張る
const MATH::Vec3 kBladeOffset{0.5f, 0.0f, 0.0f};

// 円形の形（局所 XZ、+X から測る、法線は局所 +Y）を刃面（組の XY）へ置き、弧の中心を組の +X へ向ける欧拉角。
MATH::Vec3 CircleInBladePlane(float halfArcDegrees) { return {-90.0f, 0.0f, -halfArcDegrees}; }

// 二つの域の見た目（同じ烟に見えるように揃える）：淡い青灰の烟、熱い刃口だけがかすかに白く光る。
VfxEmitterData SmokeDomain(const std::string& suffix, std::string name, const MATH::Vec3& position,
                           const MATH::Vec3& size, uint32_t resolution, bool local) {
    VfxEmitterData domain = MakePresetEmitterBase(suffix, std::move(name));
    ApplyLayerKindDefaults(domain, VfxLayerKind::Fluid);
    domain.fluidDomain.id = "module.fluid_domain." + suffix;
    domain.emitterTransform.transform.position = position;
    domain.basic.localSimulation = local;
    VfxFluidDomainModuleData& smoke = domain.fluidDomain;
    smoke.domainSize = size;
    smoke.resolution = resolution;
    smoke.boundary = RENDER3D::FLUID::FluidBoundary::Open;
    smoke.weight = 0.0f;
    smoke.scatteringColor = {0.66f, 0.69f, 0.74f};
    smoke.anisotropy = 0.3f;
    smoke.multipleScattering = 0.8f;
    smoke.ambientStrength = 1.0f;
    smoke.emissionColor = {0.75f, 0.82f, 0.95f};
    smoke.emissionHotColor = {1.0f, 1.0f, 1.0f};
    smoke.emissionTemperature = 0.3f;
    smoke.emissionHotTemperature = 1.2f;
    smoke.emissionIntensity = 200000.0f;
    smoke.castShadows = true;
    smoke.prewarmSeconds = 0.0f;
    smoke.substepRate = 60.0f;
    smoke.maxSubsteps = 3u;
    return domain;
}

VfxEmitterData SmokeSource(const std::string& suffix, std::string name, const std::string& parent, float start,
                           float active) {
    VfxEmitterData source = MakePresetEmitterBase(suffix, std::move(name));
    ApplyLayerKindDefaults(source, VfxLayerKind::FluidSource);
    source.fluidSource.id = "module.fluid_source." + suffix;
    source.parentEmitterId = parent;
    source.startDelay = start;
    source.activeDuration = active;
    source.forces.gravity = {0.0f, 0.0f, 0.0f};
    source.fluidSource.velocity = {};
    return source;
}

// 刃の烟：刃面の中の弧形の帯。刃口の帯は約 3 格しか厚みが無く、法線方向へ 1.5 倍して烟の板に見せる（3 倍は太い縄）。
VfxEmitterData BladeArc(const std::string& suffix, std::string name, const std::string& parent, float halfArc,
                        float outer, float inner, float rate, float temperature, float active) {
    VfxEmitterData arc = SmokeSource(suffix, std::move(name), parent, kLaunch, active);
    arc.emitterTransform.transform.position = {-kBladeOffset.x, -kBladeOffset.y, -kBladeOffset.z};
    arc.emitterTransform.transform.rotationEulerDegrees = CircleInBladePlane(halfArc);
    arc.emitterTransform.transform.scale = {1.0f, 1.5f, 1.0f};
    arc.shape.type = VfxShapeType::Circle;
    arc.shape.radius = MakeVfxFloatConstant(outer);
    arc.shape.circleInnerRadius = inner;
    arc.shape.circleArcDegrees = 2.0f * halfArc;
    arc.spawn.ratePerSecond = rate;
    // 注入した烟は相対風ですぐ源の外へ流れ、温度は源の値の 1/4 ほどにしか寄らない。刃口だけを 4 にして発光の起点 0.3 を
    // 越えさせる。注入を塊で揺らして均一な膜にしない。
    arc.fluidSource.temperature = temperature;
    arc.fluidSource.emissionNoise = 0.6f;
    arc.fluidSource.emissionNoiseScale = 0.6f;
    return arc;
}

VfxEmitterData Tip(const std::string& suffix, std::string name, const std::string& parent, float side, float active) {
    VfxEmitterData tip = SmokeSource(suffix, std::move(name), parent, kLaunch, active);
    const float angle = kHalfArc * (3.14159265358979323846f / 180.0f);
    const float radius = kRadius - 0.1f;
    tip.emitterTransform.transform.position = {radius * std::cos(angle), side * radius * std::sin(angle), 0.0f};
    tip.shape.type = VfxShapeType::Sphere;
    tip.shape.radius = MakeVfxFloatConstant(0.1f);
    tip.spawn.ratePerSecond = 90.0f;
    // 刃尖の外側・後ろへ少し流す。温度は低く（熱い烟は刃身の中で白い光の塊になる）。
    tip.fluidSource.temperature = 0.5f;
    tip.fluidSource.velocity = {4.0f, side * 2.0f, 0.0f};
    // 翼尖の渦：刃尖の周り 0.7 m を飛行軸（局所 +X）の周りに回す。上下の刃尖は逆回り。
    tip.forces.field.center = {0.0f, 0.0f, 0.0f};
    tip.forces.field.radius = 0.7f;
    tip.forces.field.axis = {side, 0.0f, 0.0f};
    tip.forces.field.vortex = 60.0f;
    return tip;
}
} // namespace

VfxAssetData SwordQi() {
    const std::string suffix = "fluid_sword_qi";
    const float stop = kRange - kRadius * kScaleHit;
    const float flight = stop / kSpeed;
    const float impact = kLaunch + flight;

    VfxAssetData asset;
    asset.name = "Sword Qi";
    asset.durationSeconds = 3.0f;
    asset.loop = true;

    // 路径の域：挂点の 1 m 後ろから当たる所の 1.5 m 先まで、格約 11 cm。域全体の弱い卷曲乱流で残った烟がゆっくりほどける。
    const float length = kRange + 2.5f;
    VfxEmitterData trail = SmokeDomain(suffix + ".trail", "Trail Domain", {0.5f * length - 1.0f, kHeight + 0.7f, 0.0f},
                                       {length, 6.4f, 7.0f}, 224u, false);
    trail.fluidDomain.buoyancy = 0.3f;
    trail.fluidDomain.vorticity = 2.5f;
    trail.fluidDomain.densityDissipation = 1.5f;
    trail.fluidDomain.temperatureDissipation = 4.0f;
    trail.fluidDomain.extinction = 10.0f;
    trail.fluidDomain.detailScale = 0.25f;
    trail.fluidDomain.detailStrength = 0.7f;
    trail.forces.field.turbulence = 2.5f;
    trail.forces.field.frequency = 1.5f;
    trail.forces.field.evolution = 1.0f;

    VfxEmitterData group = Shape(suffix + ".crescent", VfxRenderShapeType::Sprite, kLaunch, flight + kHold);
    group.name = "Crescent";
    group.kind = VfxLayerKind::Group;
    group.emitterTransform.transform.position = {0.0f, kHeight, 0.0f};
    group.emitterTransform.transform.rotationEulerDegrees = {kRoll, 0.0f, 0.0f};
    // 曲線の時刻は組の有効区間の割合。等速で射程まで飛び、着いたらそこで止まる。
    const float arrive = flight / (flight + kHold);
    VectorKeys(group.position.value, group.id + ".position",
               {{0.0f, {0.0f, 0.0f, 0.0f}}, {arrive, {stop, 0.0f, 0.0f}}, {1.0f, {stop, 0.0f, 0.0f}}});
    group.position.value.curve.x.interpolation = VfxCurveInterpolation::Linear;
    VectorKeys(group.scale.value, group.id + ".scale",
               {{0.0f, {kScaleStart, kScaleStart, kScaleStart}},
                {arrive, {kScaleHit, kScaleHit, kScaleHit}},
                {1.0f, {kScaleHit, kScaleHit, kScaleHit}}});

    // 刃の局所空間の域：刃面に沿って薄く（Z 1.4 m）、刃の後ろへは烟の見える 2 m あまり。相対風 0.15：烟は域の中を毎秒約
    // 4 m 後ろへ流れる（0.3 では子步 60 Hz で刃口が点の列に切れた）。発射の加速で烟を吹き飛ばさないよう慣性は 0。消散 10/s と
    // 強い乱流（24）で、刃口を出たばかりの烟は乱れず、後ろへ流れるほど丝に裂けて消える。
    VfxEmitterData blade = SmokeDomain(suffix + ".blade", "Blade Domain", kBladeOffset, {6.6f, 7.6f, 1.4f}, 224u, true);
    blade.parentEmitterId = group.id;
    blade.startDelay = kLaunch;
    blade.activeDuration = flight + 0.6f;
    blade.fluidDomain.buoyancy = 0.2f;
    blade.fluidDomain.vorticity = 3.5f;
    blade.fluidDomain.densityDissipation = 10.0f;
    blade.fluidDomain.temperatureDissipation = 8.0f;
    blade.fluidDomain.extinction = 16.0f;
    blade.fluidDomain.detailScale = 0.35f;
    blade.fluidDomain.detailStrength = 0.4f;
    blade.fluidDomain.relativeWind = 0.15f;
    blade.fluidDomain.inertia = 0.0f;
    blade.forces.field.turbulence = 24.0f;
    blade.forces.field.frequency = 2.5f;
    blade.forces.field.evolution = 2.0f;

    // 刃口：ほぼ全弧の 8 cm の帯に濃く。刃身：中段（±55°）の刃口の後ろ 0.1〜0.5 m。
    VfxEmitterData edge = BladeArc(suffix + ".edge", "Blade Edge", blade.id, kHalfArc * 0.94f, kRadius, kRadius - 0.08f,
                                   160.0f, 4.0f, flight);
    VfxEmitterData body =
        BladeArc(suffix + ".body", "Blade Body", blade.id, 55.0f, kRadius - 0.08f, kRadius - 0.5f, 70.0f, 0.0f, flight);
    VfxEmitterData tipUpper = Tip(suffix + ".tip_upper", "Tip Upper", group.id, 1.0f, flight);
    VfxEmitterData tipLower = Tip(suffix + ".tip_lower", "Tip Lower", group.id, -1.0f, flight);

    // 当たった瞬間：刃の烟を前（組の +X、円環の局所では弧の中心の向き）へ 18 m/s で押す。
    VfxEmitterData push = BladeArc(suffix + ".impact_push", "Impact Push", blade.id, kHalfArc, kRadius + 0.15f,
                                   kRadius - 0.8f, 0.0f, 0.0f, 0.1f);
    push.startDelay = impact;
    const float center = kHalfArc * (3.14159265358979323846f / 180.0f);
    push.fluidSource.velocity = {18.0f * std::cos(center), 0.0f, 18.0f * std::sin(center)};
    push.fluidSource.emissionNoise = 0.0f;

    // 当たった点（刃口の中央の少し手前、効果の空間）。域の下に無い源は両方の域へ入る。
    const MATH::Vec3 point{stop + kRadius * kScaleHit - 0.25f, kHeight, 0.0f};
    VfxEmitterData burst = SmokeSource(suffix + ".impact_burst", "Impact Burst", {}, impact, 0.08f);
    burst.emitterTransform.transform.position = point;
    burst.shape.type = VfxShapeType::Sphere;
    burst.shape.radius = MakeVfxFloatConstant(0.45f);
    burst.spawn.ratePerSecond = 25.0f;
    burst.fluidSource.temperature = 2.0f;
    burst.fluidSource.radialSpeed = 14.0f;
    burst.fluidSource.emissionNoise = 1.0f;
    burst.fluidSource.emissionNoiseScale = 0.5f;

    VfxEmitterData churn = SmokeSource(suffix + ".impact_churn", "Impact Churn", {}, impact, 0.6f);
    churn.emitterTransform.transform.position = point;
    churn.shape.type = VfxShapeType::Sphere;
    churn.shape.radius = MakeVfxFloatConstant(0.3f);
    churn.spawn.ratePerSecond = 0.0f;
    churn.fluidSource.temperature = 0.0f;
    churn.forces.field.radius = 2.2f;
    churn.forces.field.turbulence = 40.0f;
    churn.forces.field.frequency = 2.5f;
    churn.forces.field.evolution = 2.0f;

    AddKnob(asset, "smoke", "Smoke", VfxValueType::Float);
    AddKnob(asset, "glow", "Edge Glow", VfxValueType::Float);
    AddKnob(asset, "turbulence", "Turbulence", VfxValueType::Float);
    AddKnob(asset, "impact", "Impact", VfxValueType::Float);
    for (VfxEmitterData* source : {&edge, &body, &tipUpper, &tipLower})
        LinkSpawnRate(*source, "smoke");
    LinkTemperature(edge, "glow");
    LinkTurbulence(blade, "turbulence");
    LinkTurbulence(trail, "turbulence");
    LinkSpawnRate(burst, "impact");
    LinkTurbulence(churn, "impact");

    return MakePresetAssetFromEmitters(std::move(asset),
                                       {std::move(trail), std::move(group), std::move(blade), std::move(edge),
                                        std::move(body), std::move(tipUpper), std::move(tipLower), std::move(push),
                                        std::move(burst), std::move(churn)});
}

} // namespace HIKARI::VFX::PRESETS::INTERNAL
