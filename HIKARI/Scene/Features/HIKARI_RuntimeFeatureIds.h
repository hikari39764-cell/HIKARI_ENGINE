#pragma once

#include <string_view>

namespace HIKARI::RuntimeFeatureIds {

    inline constexpr std::string_view Rendering = "Rendering";
    inline constexpr std::string_view Lighting = "Lighting";
    inline constexpr std::string_view Terrain = "Terrain";
    inline constexpr std::string_view SurfaceInteraction =
        "SurfaceInteraction";
    inline constexpr std::string_view Water = "Water";
    inline constexpr std::string_view Buoyancy = "Buoyancy";
    inline constexpr std::string_view Animation = "Animation";
    inline constexpr std::string_view Camera = "Camera";
    inline constexpr std::string_view Physics = "Physics";
    inline constexpr std::string_view GameplayBasic = "GameplayBasic";
    inline constexpr std::string_view Cinematics = "Cinematics";
    inline constexpr std::string_view Vfx = "Vfx";

} // namespace HIKARI::RuntimeFeatureIds
