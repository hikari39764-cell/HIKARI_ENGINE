#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS {

    struct VfxPresetDescriptor {
        std::string id{};
        std::string displayName{};
        std::string category{};
        std::string summary{};
    };

    // Effekseer-aligned starter presets. Each factory builds a complete,
    // ready-to-edit VfxAssetData using only engine primitives (no external
    // texture dependencies), so presets work before any material is assigned.
    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog();
    const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept;
    VfxAssetData MakeVfxPresetAsset(std::string_view id);

} // namespace HIKARI::VFX::PRESETS
