#pragma once

#include <array>
#include <cstdint>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

// This is an engine-private, frame-local contract.  The scene service
// owns every pointed-to draw record and keeps it alive until the next
// service tick; renderers must not retain the pointer after the frame.
inline constexpr uint32_t kMaximumCombatAfterimageSilhouettes = 5u;
inline constexpr uint32_t kMaximumCombatAfterimageJoints = 128u;
inline constexpr uint32_t kMaximumCombatAfterimageDraws = 512u;

struct CombatAfterimageDraw {
    const MeshPrimitive* primitive = nullptr;
    MATH::Mat4 world = MATH::Mat4::Identity();
    std::array<MATH::Mat4, kMaximumCombatAfterimageJoints> jointPalette{};
    uint32_t jointCount = 0u;
    uint8_t skinned = 0u;
    uint8_t silhouette = 1u;
    uint8_t reserved[2] = {0u, 0u};
    uint32_t silhouetteIndex = 0u;
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 0.28f;
    float opacity = 0.62f;
    float intensity = 1.0f;
    float uniformScale = 1.0f;
    float fresnelPower = 3.0f;
    float dissolveStrength = 1.0f;
    float shellOffsetWorld = 0.0f;
    MATH::Vec3 color{0.10f, 0.72f, 1.0f};
};

struct CombatAfterimageFrameSource {
    const CombatAfterimageDraw* draws = nullptr;
    uint32_t drawCount = 0u;
    uint32_t silhouetteCount = 0u;
    // Presentation diagnostics. A missing evaluated pose is never
    // replaced with bind pose; the renderer only reports the rejection.
    bool missingPose = false;
    uint64_t frameIndex = 0u;
    uint64_t revision = 0u;

    bool HasAfterimages() const noexcept { return draws != nullptr && drawCount != 0u && silhouetteCount != 0u; }
};

} // namespace HIKARI::RENDER3D::COMBAT_VFX
