#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {

VfxEmitterData MakePresetEmitterBase(std::string idSuffix, std::string name);

VfxAssetData MakePresetAssetFromEmitters(VfxAssetData asset, std::vector<VfxEmitterData> emitters);

} // namespace HIKARI::VFX::PRESETS::INTERNAL
