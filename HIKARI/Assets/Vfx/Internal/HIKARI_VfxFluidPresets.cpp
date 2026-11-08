#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"
#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"

#include <stdexcept>
#include <string>
#include <utility>

// 燃焼する流体の火と爆発の预设。どれも燃焼の域（燃料が燃えて熱と煤を出し、熱い煤が温度の色帯で光って冷めると暗い烟に
// なる）と、燃料だけを入れる源（fuelFraction 1、温度は着火温度の上）でできている。値は FluidSmokeShowcase の
// Sample Burn Fire / Sample Air Blast で昼に見て決めた。厚い発光の烟の明るさは emissionIntensity / extinction なので、
// 消光 6〜8 の烟が昼の日なたの烟より明るく見えるには 30 万の桁が要る。温度は速めに冷まし（4/s）、燃えている所だけ光らせる。
namespace HIKARI::VFX::PRESETS::INTERNAL {
namespace {
VfxEmitterData BurningDomain(const std::string& suffix, const MATH::Vec3& size, uint32_t resolution) {
    VfxEmitterData domain = MakePresetEmitterBase(suffix, "Fire Domain");
    ApplyLayerKindDefaults(domain, VfxLayerKind::Fluid);
    domain.fluidDomain.id = "module.fluid_domain." + suffix;
    VfxFluidDomainModuleData& fire = domain.fluidDomain;
    fire.domainSize = size;
    fire.resolution = resolution;
    fire.combustion = true;
    fire.ignitionTemperature = 0.5f;
    fire.burnRate = 5.0f;
    fire.fuelPerBurn = 1.0f;
    fire.heatPerBurn = 3.0f;
    fire.smokePerBurn = 0.5f;
    fire.expansionPerBurn = 0.4f;
    fire.buoyancy = 3.0f;
    fire.vorticity = 2.0f;
    fire.densityDissipation = 0.8f;
    fire.temperatureDissipation = 4.0f;
    fire.extinction = 8.0f;
    fire.scatteringColor = {0.22f, 0.2f, 0.19f};
    fire.emissionColor = {1.0f, 0.16f, 0.02f};
    fire.emissionTemperature = 0.9f;
    fire.emissionHotColor = {1.0f, 0.62f, 0.22f};
    fire.emissionHotTemperature = 3.0f;
    fire.emissionIntensity = 300000.0f;
    fire.detailScale = 0.08f;
    fire.detailStrength = 0.6f;
    fire.prewarmSeconds = 2.0f;
    // 火の光：流体の発光の重心に置き、明るさと色を発光から決める（光束の上限 30 万 lm、届く範囲 8 m）。
    Light(domain, {1.0f, 1.0f, 1.0f, 1.0f}, 300000.0f, 8.0f);
    domain.specialOutputs.back().light.followFluidGlow = true;
    return domain;
}

// 燃料だけを入れる源（位置は域の局所）。乱流と注入の揺らぎで炎の舌を揺らす。
VfxEmitterData FuelSource(const std::string& suffix, const VfxEmitterData& domain, const MATH::Vec3& position) {
    VfxEmitterData source = MakePresetEmitterBase(suffix, "Fuel");
    ApplyLayerKindDefaults(source, VfxLayerKind::FluidSource);
    source.fluidSource.id = "module.fluid_source." + suffix;
    source.parentEmitterId = domain.id;
    source.emitterTransform.transform.position = position;
    source.fluidSource.fuelFraction = 1.0f;
    source.fluidSource.temperature = 1.2f;
    source.fluidSource.velocity = {0.0f, 0.8f, 0.0f};
    source.fluidSource.emissionNoise = 0.7f;
    source.fluidSource.emissionNoiseScale = 0.15f;
    source.forces.field.turbulence = 0.6f;
    source.forces.field.frequency = 4.0f;
    source.forces.field.evolution = 2.0f;
    return source;
}

// 熱気の揺らぎ（画面歪みの粒子。Niagara・Unity の熱気の粒子と同じ作り）：火の根元の箱から出て流体の流れに乗って昇るので、
// 揺らぎは熱い烟柱の形に付いて行く。層色 R が強さ（画面の 6% × 0.04 ≈ 3〜4 画素）、B 1 で底色貼図 distortion_noise の RG を
// 歪みの場にして 2 回並べて流す。G（ぼかし）は 0：戦闘の歪みの合成はぼかしに比例して接線方向の筋と色ずれを足すので、
// 熱気では炎の縁が段々に切れて緑の縁が出る。粒子は大きいほど画面を覆って描画が重い（近くの火で 1.2 m・毎秒 14 個は
// 0.7 ms、0.8 m・毎秒 8 個で 0.2 ms）。全体後処理のある画面でだけ合成される。位置は資産の原点（域の中心）から。
VfxEmitterData HeatHaze(const std::string& suffix, const MATH::Vec3& position, const MATH::Vec3& halfExtents,
                        float ratePerSecond) {
    VfxEmitterData haze = MakePresetEmitterBase(suffix, "Heat Haze");
    haze.emitterTransform.transform.position = position;
    haze.basic.localSimulation = false;
    haze.basic.lifetime = {VfxDistributionMode::RandomRange, 1.0f, 1.6f, 0u};
    haze.spawn.ratePerSecond = ratePerSecond;
    haze.spawn.maximumParticles = 128u;
    haze.shape.type = VfxShapeType::Box;
    haze.shape.boxHalfExtents = halfExtents;
    haze.position.radialSpeed = MakeVfxFloatConstant(0.0f);
    haze.forces.gravity = {0.0f, 0.0f, 0.0f};
    haze.forces.drag = 0.0f;
    haze.forces.fluidFollow = 6.0f;
    haze.renderCommon.blendMode = VfxBlendMode::Distortion;
    Texture(haze, "distortion_noise");
    Flow(haze, 0.0f, -0.4f, 2.0f, 2.0f);
    VectorKeys(haze.scale.value, haze.id + ".size", {{0, {0.35f, 0.35f, 0.35f}}, {1, {0.8f, 0.8f, 0.8f}}});
    AlphaKeys(haze, {0.04f, 0.0f, 1.0f}, {{0, 0}, {0.3f, 1}, {1, 0}});
    return haze;
}

void Sphere(VfxEmitterData& source, float radius) {
    source.shape.type = VfxShapeType::Sphere;
    source.shape.radius = MakeVfxFloatConstant(radius);
}

// 火と爆発の旋钮：燃料の量（連続の注入率と burst）、熱さ（燃料の温度）、炎の舌の乱れ（燃料の源の乱流）。
void FireKnobs(VfxAssetData& asset, VfxEmitterData& fuel) {
    AddKnob(asset, "fuel", "Fuel", VfxValueType::Float);
    AddKnob(asset, "heat", "Heat", VfxValueType::Float);
    AddKnob(asset, "turbulence", "Turbulence", VfxValueType::Float);
    LinkSpawnRate(fuel, "fuel");
    LinkTemperature(fuel, "heat");
    LinkTurbulence(fuel, "turbulence");
}

// 立ち昇る烟：底の源から温かい烟が浮力で昇り、卷曲乱流でほどけて薄れる。効果の原点は地面（域は原点から上へ 6 m）。
// 旋钮は 烟の量・熱さ（昇る速さ）・乱れ。
VfxAssetData Smoke() {
    const std::string suffix = "fluid_smoke";
    VfxAssetData asset;
    asset.name = "Smoke";
    asset.durationSeconds = 30.0f;
    asset.loop = true;
    VfxEmitterData domain = MakePresetEmitterBase(suffix + ".domain", "Smoke Domain");
    ApplyLayerKindDefaults(domain, VfxLayerKind::Fluid);
    domain.fluidDomain.id = "module.fluid_domain." + suffix;
    VfxFluidDomainModuleData& smoke = domain.fluidDomain;
    smoke.domainSize = {3.0f, 6.0f, 3.0f};
    smoke.resolution = 96u;
    smoke.buoyancy = 1.2f;
    smoke.vorticity = 2.0f;
    smoke.densityDissipation = 0.25f;
    smoke.temperatureDissipation = 0.6f;
    smoke.extinction = 7.0f;
    smoke.scatteringColor = {0.5f, 0.5f, 0.52f};
    smoke.anisotropy = 0.3f;
    smoke.detailStrength = 0.6f;
    smoke.detailScale = 0.15f;
    smoke.prewarmSeconds = 3.0f;
    domain.emitterTransform.transform.position = {0.0f, 3.0f, 0.0f};
    domain.forces.field.turbulence = 0.6f;
    domain.forces.field.frequency = 1.5f;
    domain.forces.field.evolution = 1.0f;
    VfxEmitterData source = MakePresetEmitterBase(suffix + ".source", "Smoke Source");
    ApplyLayerKindDefaults(source, VfxLayerKind::FluidSource);
    source.fluidSource.id = "module.fluid_source." + suffix;
    source.parentEmitterId = domain.id;
    source.emitterTransform.transform.position = {0.0f, -2.6f, 0.0f};
    Sphere(source, 0.45f);
    source.spawn.ratePerSecond = 12.0f;
    source.forces.gravity = {0.0f, 0.0f, 0.0f};
    source.fluidSource.temperature = 1.2f;
    source.fluidSource.velocity = {0.0f, 1.0f, 0.0f};
    source.fluidSource.emissionNoise = 0.7f;
    source.fluidSource.emissionNoiseScale = 0.5f;
    source.forces.field.turbulence = 1.2f;
    source.forces.field.frequency = 3.0f;
    source.forces.field.evolution = 1.5f;
    AddKnob(asset, "smoke", "Smoke", VfxValueType::Float);
    AddKnob(asset, "heat", "Heat", VfxValueType::Float);
    AddKnob(asset, "turbulence", "Turbulence", VfxValueType::Float);
    LinkSpawnRate(source, "smoke");
    LinkTemperature(source, "heat");
    LinkTurbulence(source, "turbulence");
    LinkTurbulence(domain, "turbulence");
    return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(source)});
}

// 魔法の霧：地を這う淡く光る霧が床に広がり、ゆっくり渦を巻く。浮力は無く少し重い（温度は光るためだけ）ので床に溜まる。
// 格ごとの色の域で、霧の色は源の層の色 × color の旋钮。効果の原点は地面（域は原点から上へ 2.5 m、底は閉じる）。
// 旋钮は 量・光・乱れ・色。
VfxAssetData MagicMist() {
    const std::string suffix = "fluid_magic_mist";
    VfxAssetData asset;
    asset.name = "Magic Mist";
    asset.durationSeconds = 30.0f;
    asset.loop = true;
    VfxEmitterData domain = MakePresetEmitterBase(suffix + ".domain", "Mist Domain");
    ApplyLayerKindDefaults(domain, VfxLayerKind::Fluid);
    domain.fluidDomain.id = "module.fluid_domain." + suffix;
    VfxFluidDomainModuleData& mist = domain.fluidDomain;
    mist.domainSize = {8.0f, 2.5f, 8.0f};
    mist.resolution = 112u;
    mist.boundary = RENDER3D::FLUID::FluidBoundary::ClosedFloor;
    mist.cellColor = true;
    mist.buoyancy = 0.0f;
    mist.weight = 0.4f;
    mist.vorticity = 1.5f;
    mist.densityDissipation = 0.5f;
    mist.temperatureDissipation = 0.8f;
    mist.extinction = 2.5f;
    mist.scatteringColor = {0.85f, 0.88f, 1.0f};
    mist.anisotropy = 0.4f;
    mist.multipleScattering = 0.9f;
    mist.emissionColor = {0.3f, 0.55f, 1.0f};
    mist.emissionTemperature = 0.3f;
    mist.emissionHotColor = {0.7f, 0.85f, 1.0f};
    mist.emissionHotTemperature = 1.5f;
    mist.emissionIntensity = 60000.0f;
    mist.detailStrength = 0.7f;
    mist.detailScale = 0.3f;
    mist.prewarmSeconds = 4.0f;
    domain.emitterTransform.transform.position = {0.0f, 1.25f, 0.0f};
    domain.forces.field.turbulence = 0.8f;
    domain.forces.field.frequency = 0.6f;
    domain.forces.field.evolution = 0.5f;
    domain.forces.field.vortex = 0.3f;
    VfxEmitterData source = MakePresetEmitterBase(suffix + ".source", "Mist Source");
    ApplyLayerKindDefaults(source, VfxLayerKind::FluidSource);
    source.fluidSource.id = "module.fluid_source." + suffix;
    source.parentEmitterId = domain.id;
    source.emitterTransform.transform.position = {0.0f, -1.0f, 0.0f};
    source.shape.type = VfxShapeType::Cylinder;
    source.shape.radius = MakeVfxFloatConstant(2.5f);
    source.shape.cylinderHeight = 0.3f;
    source.spawn.ratePerSecond = 1.8f;
    source.forces.gravity = {0.0f, 0.0f, 0.0f};
    source.color.value.fixed = MakeVfxColorConstant({0.55f, 0.75f, 1.0f, 1.0f});
    source.fluidSource.temperature = 0.8f;
    source.fluidSource.velocity = {0.0f, 0.0f, 0.0f};
    source.fluidSource.radialSpeed = 0.8f;
    source.fluidSource.emissionNoise = 0.9f;
    source.fluidSource.emissionNoiseScale = 0.4f;
    AddKnob(asset, "amount", "Amount", VfxValueType::Float);
    AddKnob(asset, "glow", "Glow", VfxValueType::Float);
    AddKnob(asset, "turbulence", "Turbulence", VfxValueType::Float);
    AddKnob(asset, "color", "Color", VfxValueType::Color);
    LinkSpawnRate(source, "amount");
    LinkTemperature(source, "glow");
    LinkTurbulence(domain, "turbulence");
    LinkColor(source, "color");
    return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(source)});
}

// 一度きりの燃料の塊が外へ吹き出しながら一気に燃え、火の玉が暗い烟の中の火の塊になり、燃え尽きた煤が浮力で菜花状に昇る。
// burst の数はそのまま格の燃料の量（3〜4）。体積は exp(expansionPerBurn × 燃えた燃料) 倍に膨らむ：膨張 1（約 20 倍）は
// 滑らかな大きな穹顶になり格ごとの横縞が出る、0 は小さくまとまる、0.3（約 3 倍）が菜花状に崩れて縞も出ない。
VfxAssetData Explosion(std::string name, const std::string& suffix, const MATH::Vec3& size, float sourceHeight,
                       float radius, uint32_t fuel, float outward, float smoke) {
    VfxAssetData asset;
    asset.name = std::move(name);
    asset.durationSeconds = 8.0f;
    asset.loop = false;
    VfxEmitterData domain = BurningDomain(suffix + ".domain", size, 96u);
    VfxFluidDomainModuleData& blast = domain.fluidDomain;
    blast.ignitionTemperature = 0.3f;
    blast.burnRate = 12.0f;
    blast.heatPerBurn = 3.0f;
    blast.smokePerBurn = smoke;
    blast.expansionPerBurn = 0.3f;
    blast.buoyancy = 1.2f;
    blast.vorticity = 1.5f;
    blast.densityDissipation = 0.25f;
    blast.temperatureDissipation = 2.5f;
    blast.extinction = 6.0f;
    blast.scatteringColor = {0.25f, 0.23f, 0.22f};
    blast.detailScale = 0.2f;
    blast.prewarmSeconds = 0.0f;
    VfxEmitterData charge = FuelSource(suffix + ".fuel", domain, {0.0f, sourceHeight, 0.0f});
    Sphere(charge, radius);
    charge.spawn.ratePerSecond = 0.0f;
    charge.spawn.burstTimeline = {{"burst." + suffix, 0.1f, fuel}};
    charge.fluidSource.temperature = 2.0f;
    charge.fluidSource.velocity = {};
    charge.fluidSource.radialSpeed = outward;
    charge.fluidSource.emissionNoiseScale = 0.4f;
    charge.forces.field.turbulence = 0.8f;
    charge.forces.field.frequency = 1.0f;
    charge.forces.field.evolution = 1.0f;
    FireKnobs(asset, charge);
    return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(charge)});
}
} // namespace

VfxAssetData FluidPreset(std::string_view id) {
    if (id == "fluid_sword_qi")
        return SwordQi();
    if (id == "fluid_smoke")
        return Smoke();
    if (id == "fluid_magic_mist")
        return MagicMist();
    if (id == "fluid_ground_explosion")
        return Explosion("Ground Explosion", "fluid_ground_explosion", {10.0f, 8.0f, 10.0f}, -3.4f, 0.8f, 4u, 8.0f,
                         1.5f);
    if (id == "fluid_air_explosion")
        return Explosion("Air Explosion", "fluid_air_explosion", {8.0f, 8.0f, 8.0f}, -1.5f, 0.6f, 3u, 6.0f, 1.2f);
    VfxAssetData asset;
    asset.durationSeconds = 30.0f;
    asset.loop = true;
    const std::string suffix(id);
    if (id == "fluid_fire") {
        asset.name = "Fire";
        VfxEmitterData domain = BurningDomain(suffix + ".domain", {2.5f, 4.0f, 2.5f}, 96u);
        // 開いた底面に近いと熱い烟が地を這うので、燃料は底から 0.7 m 上。
        VfxEmitterData fuel = FuelSource(suffix + ".fuel", domain, {0.0f, -1.3f, 0.0f});
        Sphere(fuel, 0.25f);
        VfxEmitterData haze = HeatHaze(suffix + ".haze", {0.0f, -1.0f, 0.0f}, {0.2f, 0.1f, 0.2f}, 8.0f);
        FireKnobs(asset, fuel);
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel), std::move(haze)});
    }
    if (id == "fluid_fire_wall") {
        asset.name = "Fire Wall";
        VfxEmitterData domain = BurningDomain(suffix + ".domain", {8.0f, 3.0f, 2.0f}, 128u);
        VfxEmitterData fuel = FuelSource(suffix + ".fuel", domain, {0.0f, -0.9f, 0.0f});
        fuel.shape.type = VfxShapeType::Box;
        fuel.shape.boxHalfExtents = {3.5f, 0.08f, 0.25f};
        fuel.spawn.ratePerSecond = 5.0f;
        VfxEmitterData haze = HeatHaze(suffix + ".haze", {0.0f, -0.6f, 0.0f}, {3.4f, 0.1f, 0.25f}, 24.0f);
        FireKnobs(asset, fuel);
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel), std::move(haze)});
    }
    if (id == "fluid_fireball") {
        // 特效に付いて動く局所空間の域の中で燃え続ける玉（VfxView::SetVelocity で飛ばす）。浮力を弱め、煤は速く消して玉の
        // 形を保つ。燃え続ける源に膨張を付けると域全体へ膨らみ続けるので 0、燃料は源の近くで燃え切るよう速く燃やす。
        asset.name = "Fireball";
        VfxEmitterData domain = BurningDomain(suffix + ".domain", {3.0f, 3.0f, 3.0f}, 64u);
        domain.basic.localSimulation = true;
        domain.fluidDomain.buoyancy = 0.5f;
        domain.fluidDomain.burnRate = 8.0f;
        domain.fluidDomain.smokePerBurn = 0.25f;
        domain.fluidDomain.expansionPerBurn = 0.0f;
        domain.fluidDomain.densityDissipation = 4.0f;
        domain.fluidDomain.prewarmSeconds = 1.0f;
        VfxEmitterData fuel = FuelSource(suffix + ".fuel", domain, {0.0f, 0.0f, 0.0f});
        Sphere(fuel, 0.45f);
        fuel.spawn.ratePerSecond = 8.0f;
        fuel.fluidSource.velocity = {};
        fuel.fluidSource.radialSpeed = 0.5f;
        fuel.fluidSource.temperature = 1.5f;
        FireKnobs(asset, fuel);
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel)});
    }
    throw std::invalid_argument("Unknown fluid preset: " + suffix);
}

} // namespace HIKARI::VFX::PRESETS::INTERNAL
