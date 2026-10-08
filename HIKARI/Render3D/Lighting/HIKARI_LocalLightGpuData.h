#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

inline constexpr uint32_t kMaximumLocalShadowFaceCount = 16u;
// 点・スポットの記録に詰める配光とクッキーの記述子番号 + 1 の桁（材質 texture pool の 3968 を収める）。
inline constexpr uint32_t kLocalLightTextureIndexStride = 4096u;

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
    Tube = 2u,
    Rect = 3u,
};

// C++ / HLSL 共通の StructuredBuffer レコード。
struct alignas(16) LocalLightGpuData {
    MATH::Vec4 positionRange{};
    MATH::Vec4 colorIntensity{};
    // 点: 配光の向き（ローカル +Z）。スポット: 向きと内側の cos。管: 軸と半長。長方形: 発光面の法線と半幅。
    MATH::Vec4 directionInnerCos{};
    float outerConeCos = -1.0f;
    float angularFalloff = 1.0f;
    // 下位 8 bit: type、次の 8 bit: falloff、上位16 bit: shadow先頭face+1 (0は無効)。
    uint32_t kind = 0u;
    // 点・スポットは発光球、管は管の半径。
    float sourceRadius = 0.0f;
    // 長方形: 幅の向き（xyz）と半高（w）。点・スポット: 配光とクッキーの水平の基準（ローカル +X）と、
    // w = 配光の記述子番号 + 1 + (クッキーの記述子番号 + 1) * kLocalLightTextureIndexStride（整数値、0 は無し、補間しない）。
    MATH::Vec4 shapeTangent{};
};

static_assert(sizeof(LocalLightGpuData) == 80u);
static_assert(alignof(LocalLightGpuData) == 16u);
static_assert(offsetof(LocalLightGpuData, kind) == 56u);
static_assert(offsetof(LocalLightGpuData, sourceRadius) == 60u);

} // namespace HIKARI::RENDER3D::LIGHTING
