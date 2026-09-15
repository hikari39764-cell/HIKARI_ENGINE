#pragma once

#include <array>
#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::SHADOW {

    inline constexpr uint32_t kMaxDirectionalShadowCascades = 4u;
    inline constexpr uint32_t kMaxLocalLightShadowFaces = 6u;

    struct ShadowLightFrame {
        MATH::Mat4 view{};
        MATH::Mat4 viewProj{};
        MATH::Vec3 anchor{};
        MATH::Vec3 lightPosition{};
        MATH::Vec3 lightDirection{};
        MATH::Vec3 right{};
        MATH::Vec3 up{};
        float anchorGrid = 0.0f;
        float splitNear = 0.0f;
        float splitFar = 0.0f;
        float orthoSize = 0.0f;
        float lightNear = 0.0f;
        float lightFar = 0.0f;
        float depthBiasScale = 1.0f;
    };

    struct DirectionalShadowFrameSet {
        std::array<ShadowLightFrame, kMaxDirectionalShadowCascades> cascades{};
        uint32_t cascadeCount = 1u;
        MATH::Vec4 splitDistances{};
    };

    struct LocalLightShadowFrameSet {
        std::array<ShadowLightFrame, kMaxLocalLightShadowFaces> faces{};
        uint32_t faceCount = 0;
    };

    // Point: +X,-X,+Y,-Y,+Z,-Z。Spot は一面。range と cone は照明と同じ契約。
    LocalLightShadowFrameSet BuildLocalLightShadowFrameSet(
        const RENDER3D::LIGHTING::SceneLightProvider& light, uint32_t faceResolution) noexcept;

    uint32_t ResolveShadowResolution(uint32_t resolution);

    DirectionalShadowFrameSet BuildDirectionalShadowFrameSet(
        const SceneEnvironment& environment,
        const Camera3D& camera,
        uint32_t cascadeResolution);

    // XY 受光範囲と交差する投影球だけが、光の上流側の深度を拡張する。
    float GetShadowCasterMinimumDepth(
        const ShadowLightFrame& frame, const MATH::Vec4& worldCenterRadius) noexcept;
    void ExtendShadowCasterDepth(ShadowLightFrame& frame, float minimumDepth) noexcept;

} // namespace HIKARI::SHADOW
