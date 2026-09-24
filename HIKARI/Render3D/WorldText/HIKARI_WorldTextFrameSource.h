#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Text/HIKARI_SdfTextLayout.h"

namespace HIKARI::RENDER3D::WORLD_TEXT {

enum class WorldTextFacingMode : uint8_t {
    Fixed,
    CameraFacing,
    FixedYAxis,
};

struct WorldTextInstance {
    uint64_t objectId = 0u;
    uint32_t stableOrder = 0u;
    MATH::Vec3 worldPosition{};
    MATH::Quat worldRotation{};
    MATH::Vec3 worldScale{1.0f, 1.0f, 1.0f};
    MATH::Vec2 layoutSize{4.0f, 1.0f};
    MATH::Vec2 pivot{0.5f, 0.5f};
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    MATH::Vec3 outlineColor{};
    std::string absoluteFontAssetPath{};
    uint64_t fontRevision = 0;
    std::string utf8Text{};
    float fontHeight = 0.5f;
    float lineHeightMultiplier = 1.2f;
    float letterSpacing = 0.0f;
    float outlineWidth = 0.04f;
    float opacity = 1.0f;
    float revealAmount = 1.0f;
    int32_t transparentSortPriority = 0;
    TEXT_RENDERING::SdfTextHorizontalAlign horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Center;
    TEXT_RENDERING::SdfTextVerticalAlign verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Center;
    TEXT_RENDERING::SdfTextWrap wrap = TEXT_RENDERING::SdfTextWrap::Character;
    WorldTextFacingMode facing = WorldTextFacingMode::CameraFacing;
    Bounds worldBounds{};
};

struct WorldTextFrameSource {
    std::vector<WorldTextInstance> instances{};
    uint64_t worldIdentity = 0u;
    uint64_t frameIndex = 0u;

    bool HasFrameWork() const noexcept { return !instances.empty(); }

    void Clear() {
        instances.clear();
        worldIdentity = 0u;
        frameIndex = 0u;
    }
};

} // namespace HIKARI::RENDER3D::WORLD_TEXT
