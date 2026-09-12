#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

    enum class LocalLightType : uint32_t {
        Point = 0u,
        Spot = 1u,
    };

    // C++ / HLSL 共通の StructuredBuffer レコード。
    struct alignas(16) LocalLightGpuData {
        MATH::Vec4 positionRange{};
        MATH::Vec4 colorIntensity{};
        MATH::Vec4 directionInnerCos{};
        // w: LocalLightFalloff の数値。構造体サイズは 64 bytes を維持する。
        MATH::Vec4 outerCosFalloffTypeFlags{};
    };

    static_assert(sizeof(LocalLightGpuData) == 64u);
    static_assert(alignof(LocalLightGpuData) == 16u);

} // namespace HIKARI::RENDER3D::LIGHTING
