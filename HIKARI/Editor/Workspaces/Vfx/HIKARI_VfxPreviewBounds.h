#pragma once

#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {

    struct VfxPreviewBounds {
        bool valid = false;
        MATH::Vec3 minimum{};
        MATH::Vec3 maximum{};
        MATH::Vec3 center{};
        float radius = 0.0f;
    };

    // Estimates the complete authored particle envelope, including the
    // emission shape, lifetime motion, gravity, local position animation and
    // render-primitive size. This is camera framing data only; preview and
    // runtime still execute the same compiled simulation and draw pipeline.
    VfxPreviewBounds EstimateVfxPreviewBounds(
        const VFX::VfxAssetData& asset,
        std::string_view emitterId = {});

} // namespace HIKARI::EDITOR
