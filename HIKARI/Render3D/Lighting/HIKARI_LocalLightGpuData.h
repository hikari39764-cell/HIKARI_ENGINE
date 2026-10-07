#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

inline constexpr uint32_t kMaximumLocalShadowFaceCount = 16u;

// Surface / volume の receiver が同じ完全な atlas publication を参照する。
struct alignas(16) LocalShadowGpuData {
    std::array<MATH::Mat4, kMaximumLocalShadowFaceCount> viewProj{};
    uint32_t faceCount = 0;
    uint32_t textureDescriptorIndex = UINT32_MAX;
    uint32_t tilesPerRow = 0;
    uint32_t tileResolution = 0;
};
static_assert(sizeof(LocalShadowGpuData) == 1040u);

enum class LocalLightType : uint32_t {
    Point = 0u,
    Spot = 1u,
};

// C++ / HLSL 共通の StructuredBuffer レコード。
struct alignas(16) LocalLightGpuData {
    MATH::Vec4 positionRange{};
    MATH::Vec4 colorIntensity{};
    MATH::Vec4 directionInnerCos{};
    float outerConeCos = -1.0f;
    float angularFalloff = 1.0f;
    // 下位 8 bit: type、次の 8 bit: falloff、上位16 bit: shadow先頭face+1 (0は無効)。
    uint32_t kind = 0u;
    float sourceRadius = 0.0f;
};

static_assert(sizeof(LocalLightGpuData) == 64u);
static_assert(alignof(LocalLightGpuData) == 16u);
static_assert(offsetof(LocalLightGpuData, kind) == 56u);
static_assert(offsetof(LocalLightGpuData, sourceRadius) == 60u);

} // namespace HIKARI::RENDER3D::LIGHTING
