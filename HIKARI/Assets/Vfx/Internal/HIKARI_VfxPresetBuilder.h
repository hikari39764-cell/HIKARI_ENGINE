#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {

    VfxEmitterData MakePresetEmitterBase(
        std::string idSuffix,
        std::string name);

    VfxAssetData MakePresetAssetFromEmitters(
        VfxAssetData asset,
        std::vector<VfxEmitterData> emitters);

    void SetPresetColorStops(
        VfxEmitterData& emitter,
        const MATH::Vec4& start,
        const MATH::Vec4& end);

} // namespace HIKARI::VFX::PRESETS::INTERNAL
