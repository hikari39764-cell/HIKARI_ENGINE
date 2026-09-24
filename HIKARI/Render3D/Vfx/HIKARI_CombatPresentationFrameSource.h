#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

struct WeaponTrailVertex {
    MATH::Vec3 position{};
    MATH::Vec2 uv{};
    MATH::Vec4 color{};
    float emissiveIntensity = 1.0f;
    float flowStrength = 0.0f;
    // World-space directions are carried with the history sample rather
    // than reconstructed from screen derivatives.  This keeps the
    // refraction flow stable when the blade bends, the camera pans, or
    // the trail crosses a silhouette.
    MATH::Vec3 tangentWorld{0.0f, 0.0f, 1.0f};
    MATH::Vec3 normalWorld{0.0f, 1.0f, 0.0f};
    // x = phase, y = coverage, z = authored blur, w = distortion strength.
    MATH::Vec4 flowData{};
};

struct GroundTelegraphVertex {
    MATH::Vec3 position{};
    MATH::Vec4 color{};
    MATH::Vec2 uv{};
    // x = style (0 ground, 1 barrier), y = interaction strength.
    MATH::Vec2 effect{};
};

struct CombatPresentationFrameSource {
    const WeaponTrailVertex* weaponTrailVertices = nullptr;
    uint32_t weaponTrailVertexCount = 0u;
    uint32_t activeWeaponTrailCount = 0u;
    const GroundTelegraphVertex* groundTelegraphVertices = nullptr;
    uint32_t groundTelegraphVertexCount = 0u;
    uint32_t activeGroundTelegraphCount = 0u;
    uint32_t activeWorldBarrierCount = 0u;
    // Absolute presentation frame supplied by the host. This is not the
    // graphics frame-resource slot and is used to reject stale masks.
    uint64_t frameIndex = 0u;
    uint64_t revision = 0u;
    float unscaledTimeSeconds = 0.0f;
    float reserved[3] = {0.0f, 0.0f, 0.0f};
    // Frame-local typed HVFX profile projection. The mask renderer owns
    // the GPU interpretation; Scene only publishes sanitized values.
    float distortionStrengthScale = 1.0f;
    float blurScale = 1.0f;

    bool HasWeaponTrails() const noexcept { return weaponTrailVertices != nullptr && weaponTrailVertexCount >= 3u; }

    bool HasGroundTelegraphs() const noexcept {
        return groundTelegraphVertices != nullptr && groundTelegraphVertexCount >= 3u;
    }
};

} // namespace HIKARI::RENDER3D::COMBAT_VFX
