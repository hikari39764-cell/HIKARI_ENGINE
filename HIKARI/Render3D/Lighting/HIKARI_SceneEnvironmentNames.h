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

inline constexpr TEXT::EnumName<SkySource> kSkySourceNames[] = {
    {SkySource::Atmosphere, "Atmosphere"},
    {SkySource::Hdri, "Hdri"},
    {SkySource::Panorama, "Panorama"},
};

inline constexpr TEXT::EnumName<SunMode> kSunModeNames[] = {
    {SunMode::Celestial, "Celestial"},
    {SunMode::Fixed, "Fixed"},
};

inline constexpr TEXT::EnumName<ScreenSpaceReflectionQuality> kScreenSpaceReflectionQualityNames[] = {
    {ScreenSpaceReflectionQuality::Off, "Off"},
    {ScreenSpaceReflectionQuality::Low, "Low"},
    {ScreenSpaceReflectionQuality::Medium, "Medium"},
    {ScreenSpaceReflectionQuality::High, "High"},
};

} // namespace HIKARI
