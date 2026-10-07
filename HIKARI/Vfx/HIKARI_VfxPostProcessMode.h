#pragma once

#include <cstdint>
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::VFX {

// 描画層の scene-linear HDR 合成先。露出・tone mapping は常に共通の最終出力が所有する。
enum class VfxPostProcessMode : uint8_t { Scene, PreserveColor };
inline constexpr TEXT::EnumName<VfxPostProcessMode> kVfxPostProcessModeNames[]{
    {VfxPostProcessMode::Scene, "Scene"},
    {VfxPostProcessMode::PreserveColor, "PreserveColor"},
};
inline constexpr auto kVfxPostProcessModeChoices = TEXT::EnumNameList(kVfxPostProcessModeNames);

} // namespace HIKARI::VFX
