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

void Sphere(VfxEmitterData& source, float radius) {
    source.shape.type = VfxShapeType::Sphere;
    source.shape.radius = MakeVfxFloatConstant(radius);
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
    return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(charge)});
}
} // namespace

VfxAssetData FluidPreset(std::string_view id) {
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
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel)});
    }
    if (id == "fluid_fire_wall") {
        asset.name = "Fire Wall";
        VfxEmitterData domain = BurningDomain(suffix + ".domain", {8.0f, 3.0f, 2.0f}, 128u);
        VfxEmitterData fuel = FuelSource(suffix + ".fuel", domain, {0.0f, -0.9f, 0.0f});
        fuel.shape.type = VfxShapeType::Box;
        fuel.shape.boxHalfExtents = {3.5f, 0.08f, 0.25f};
        fuel.spawn.ratePerSecond = 5.0f;
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel)});
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
        return MakePresetAssetFromEmitters(std::move(asset), {std::move(domain), std::move(fuel)});
    }
    throw std::invalid_argument("Unknown fluid preset: " + suffix);
}

} // namespace HIKARI::VFX::PRESETS::INTERNAL
