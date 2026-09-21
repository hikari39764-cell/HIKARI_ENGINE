#pragma once
#include <array>
namespace HIKARI::EDITOR {
    inline constexpr std::array kCinematicChannelKeys{
        "cine.eyeX", "cine.eyeY", "cine.eyeZ", "cine.targetX", "cine.targetY", "cine.targetZ", "cine.fov", "cine.roll"};
    inline constexpr std::array kCinematicEffectKeys{
        "cine.bloom", "cine.bloomThreshold", "cine.exposure", "cine.saturation", "cine.contrast", "cine.temperature",
        "cine.vignette", "cine.monochrome", "cine.tint", "cine.fade", "cine.pixelate", "cine.chromatic"};
}
