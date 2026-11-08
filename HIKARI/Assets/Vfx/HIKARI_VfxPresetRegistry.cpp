#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"
#include <stdexcept>

namespace HIKARI::VFX::PRESETS {
const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog() {
    static const std::vector<VfxPresetDescriptor> presets{
        {"slash", "Azure Cleave", "Compositions",
         "Fixed swing plane, advected membrane, blade history and cut fragments"},
        {"shockwave", "Shockwave", "Compositions", "Expanding ground ring with layered glow"},
        {"beam", "Ion Lance", "Compositions", "Condensation, coherent core, spiral flow and sustained contact"},
        {"shield", "Shield", "Compositions", "Mesh dome with glow and drifting motes"},
        {"charge", "Charge", "Compositions", "Collapsing energy sphere and motes"},
        {"flame_jet", "Flame Jet", "Compositions", "Advected flame flipbook, aligned embers and cooling smoke"},
        {"fire_tornado", "Fire Tornado", "Compositions",
         "Layered spiral fire, rising tongues, ground intake, orbiting embers and cooling smoke"},
        {"impact", "Impact", "Compositions", "Expanding impact mesh and short sparks"},
        {"ribbon_trail", "Motion Trail", "Compositions",
         "Continuous textured ribbon following animated world-space edges"},
        {"sparks", "Sparks", "Particles", "Short streaks under gravity"},
        {"smoke", "Smoke / Dust", "Particles", "Lit soft smoke flipbook"},
        {"motes", "Glow Motes", "Particles", "Slow drifting glow particles"},
        {"debris", "Debris", "Particles", "Rotating fragments with ground bounce"},
        {"style_pixel", "Pixel Art", "Styles", "Pixelation and ordered color dithering"},
        {"style_ink", "Ink Wash", "Styles", "Pigment edges, bleeding and paper grain"},
        {"style_comic", "Comic Print", "Styles", "Outline, limited tones and halftone dots"},
        {"style_hologram", "Hologram", "Styles", "Two-tone color, scanlines and chromatic separation"},
        {"style_heat", "Heat Haze", "Styles", "Evolving displacement and subtle chromatic separation"},
        {"style_glow", "Soft Glow", "Styles", "Soft bright extraction and streaks"},
        {"fluid_fire", "Fire", "Fluid",
         "Burning fuel: glowing soot lights the scene, rises with heat haze and cools into dark smoke"},
        {"fluid_fire_wall", "Fire Wall", "Fluid", "Long strip of burning fuel"},
        {"fluid_fireball", "Fireball", "Fluid",
         "Self-contained burning ball that follows the effect (fly it with velocity)"},
        {"fluid_ground_explosion", "Ground Explosion", "Fluid",
         "Fuel burst on the ground: expanding fireball, dust and a rising smoke column"},
        {"fluid_air_explosion", "Air Explosion", "Fluid",
         "Fuel burst in the air: fireball that burns out into a rising mushroom of smoke"},
        {"fluid_smoke", "Smoke", "Fluid", "Warm smoke rising from a source and unravelling with turbulence"},
        {"fluid_magic_mist", "Magic Mist", "Fluid", "Faintly glowing colored mist creeping over the floor"},
        {"fluid_sword_qi", "Sword Qi", "Fluid",
         "Crescent of smoke flying forward: dense edge, torn trailing smoke, tip vortices and an impact burst"}};
    return presets;
}

void ApplyLayerKindDefaults(VfxEmitterData& emitter, VfxLayerKind kind) {
    emitter.kind = kind;
    if (kind != VfxLayerKind::Particles) {
        emitter.renderShape.billboardMode = VfxBillboardMode::Fixed;
        emitter.spawn.ratePerSecond = 0;
        emitter.spawn.burstTimeline.clear();
        emitter.renderCommon.colorTextureAssetGuid = {};
    }
    if (kind == VfxLayerKind::Fluid || kind == VfxLayerKind::FluidSource) {
        // 流体の層は効果・模型出力・特殊出力・履歴・遮罩を持たない。
        emitter.effects.clear();
        emitter.specialOutputs.clear();
        emitter.meshOutput.enabled = false;
        emitter.history.mode = VfxHistoryMode::None;
        emitter.matte = {};
        // 層の色は烟の色と消光の倍率になるので、既定の層の寿命の色（淡くなる曲線）を使わず白の一定にする。
        emitter.color.value.mode = VfxAnimatedColorMode::Fixed;
        emitter.color.value.fixed = {
            VfxDistributionMode::Constant, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0u};
    }
    if (kind == VfxLayerKind::FluidSource) {
        emitter.spawn.ratePerSecond = 6.0f;
        emitter.shape.type = VfxShapeType::Sphere;
        emitter.shape.radius = {VfxDistributionMode::Constant, 0.35f, 0.35f, 0u};
    }
}

const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept {
    for (const auto& preset : GetVfxPresetCatalog())
        if (preset.id == id)
            return &preset;
    return nullptr;
}

VfxAssetData MakeVfxPresetAsset(std::string_view id) {
    const auto* preset = FindVfxPreset(id);
    if (!preset)
        throw std::invalid_argument("Unknown VFX preset: " + std::string(id));
    if (preset->category == "Styles")
        return INTERNAL::StyleModule(id);
    if (id == "slash")
        return INTERNAL::Slash();
    if (id == "beam")
        return INTERNAL::Beam();
    if (id == "fire_tornado")
        return INTERNAL::FireTornado();
    if (preset->category == "Fluid")
        return INTERNAL::FluidPreset(id);
    if (preset->category == "Compositions")
        return INTERNAL::SupportingComposition(id);
    return INTERNAL::ParticlePreset(id);
}

} // namespace HIKARI::VFX::PRESETS
