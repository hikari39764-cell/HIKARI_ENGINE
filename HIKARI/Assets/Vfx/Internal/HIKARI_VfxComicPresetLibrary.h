#pragma once

#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {

    bool IsComicPresetId(std::string_view id) noexcept;
    VfxAssetData BuildComicPreset(std::string_view id);
    bool ApplyComicPresetTextures(
        std::string_view id,
        VfxAssetData& asset);

} // namespace HIKARI::VFX::PRESETS::INTERNAL
