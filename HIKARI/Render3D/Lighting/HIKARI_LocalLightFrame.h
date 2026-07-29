#pragma once

#include <array>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::LIGHTING {

    inline constexpr uint32_t kLocalLightFrameCapacity = 8u;

    enum class LocalLightType : uint32_t {
        Point = 0u,
        Spot = 1u,
    };

    // C++ / HLSL 共通の Local Light GPU レコード。
    // StructuredBuffer への移行後も同じレコードを利用できるよう、
    // 現在の Point Light 以外の形状パラメータもレイアウトに含める。
    struct alignas(16) LocalLightGpuData {
        MATH::Vec4 positionRange{};
        MATH::Vec4 colorIntensity{};
        MATH::Vec4 directionInnerCos{};
        MATH::Vec4 outerCosFalloffTypeFlags{};
    };

    static_assert(sizeof(LocalLightGpuData) == 64u);
    static_assert(alignof(LocalLightGpuData) == 16u);

    struct LocalLightFrame {
        std::array<LocalLightGpuData, kLocalLightFrameCapacity> lights{};
        uint32_t sourceCount = 0u;
        uint32_t uploadableCount = 0u;
        uint32_t uploadedCount = 0u;
        uint32_t clampedCount = 0u;
    };

    // Scene authoring semantics を GPU レコードへ変換する唯一の入口。
    // 現段階では既存 Point Light の見た目と 8 灯上限を維持する。
    LocalLightFrame BuildLocalLightFrame(
        const SceneEnvironment& environment);

} // namespace HIKARI::RENDER3D::LIGHTING
