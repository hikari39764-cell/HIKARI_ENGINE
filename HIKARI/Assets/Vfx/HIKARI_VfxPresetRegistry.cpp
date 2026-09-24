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
        {"style_glow", "Soft Glow", "Styles", "Soft bright extraction and streaks"}};
    return presets;
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
    if (preset->category == "Compositions")
        return INTERNAL::SupportingComposition(id);
    return INTERNAL::ParticlePreset(id);
}

} // namespace HIKARI::VFX::PRESETS
