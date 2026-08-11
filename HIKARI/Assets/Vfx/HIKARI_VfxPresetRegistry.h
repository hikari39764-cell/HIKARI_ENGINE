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

    struct VfxPresetPackage {
        VfxAssetData asset{};
    };

    // Effekseer-aligned starter presets reference reusable EngineContent
    // textures directly through Render Common. Custom Material authoring is
    // an explicit user choice and never generated as preset-private state.
    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog();
    const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept;
    VfxAssetData MakeVfxPresetAsset(std::string_view id);
    VfxPresetPackage MakeVfxPresetPackage(std::string_view id);

} // namespace HIKARI::VFX::PRESETS
