#pragma once

#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

inline constexpr TEXT::EnumName<SsaoMode> kSsaoModeNames[] = {
    {SsaoMode::Off, "Off"},
    {SsaoMode::Reference, "Reference"},
    {SsaoMode::OptimizedHigh, "OptimizedHigh"},
    {SsaoMode::Balanced, "Balanced"},
};

inline const char* GetSsaoModeName(SsaoMode mode) noexcept { return TEXT::EnumToName(kSsaoModeNames, mode, "Off"); }

} // namespace HIKARI
