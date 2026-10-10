#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

// 局所光の影の面の上限（高画質で点光 5 灯 + スポット 2 灯）。HIKARI_LocalLightContract.hlsli の viewProj と同じ数。
inline constexpr uint32_t kMaximumLocalShadowFaceCount = 32u;
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
static_assert(sizeof(LocalShadowGpuData) == 2064u);

enum class LocalLightType : uint32_t {
    Point = 0u,
    Spot = 1u,
    Tube = 2u,
    Rect = 3u,
    // 光源の後ろに並ぶデカールの記録（LocalDecalGpuData）。光源の走査には現れない。
    Decal = 4u,
};

// C++ / HLSL 共通の StructuredBuffer レコード。
struct alignas(16) LocalLightGpuData {
    MATH::Vec4 positionRange{};
    MATH::Vec4 colorIntensity{};
    // 点: 配光の向き（ローカル +Z）。スポット: 向きと内側の cos。管: 軸と半長。長方形: 発光面の法線と半幅。
    MATH::Vec4 directionInnerCos{};
    float outerConeCos = -1.0f;
    float angularFalloff = 1.0f;
    // 下位 8 bit: type、上位 16 bit: shadow 先頭 face + 1（0 は無効）。
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

// デカールの記録。局所光と同じ StructuredBuffer に載るので大きさと kind の位置を揃える（HIKARI_LocalDecals.hlsli）。
struct alignas(16) LocalDecalGpuData {
    // ワールドから箱の空間 [-0.5, 0.5]^3 への三行（xyz に掛けて w を足す）。
    std::array<MATH::Vec4, 3> worldToDecal{};
    // 色と法線の記述子番号 + 1 を整数値で（色 + 法線 * kLocalLightTextureIndexStride、0 は無し）。
    float textures = 0.0f;
    // 薄れ始めの cos（面の法線と投影の逆向き）。0 で消える。
    float angleFadeCos = 0.5f;
    // 下位 8 bit: LocalLightType::Decal、上位 24 bit: 色（RGB 各 8 bit）。
    uint32_t kind = 0u;
    // 色・法線・粗さと金属度の重み（opacity 込み）と、寄せる粗さと金属度。
    float colorWeight = 0.0f;
    float normalWeight = 0.0f;
    float materialWeight = 0.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
};

static_assert(sizeof(LocalDecalGpuData) == sizeof(LocalLightGpuData));
static_assert(offsetof(LocalDecalGpuData, kind) == offsetof(LocalLightGpuData, kind));
static_assert(offsetof(LocalDecalGpuData, colorWeight) == offsetof(LocalLightGpuData, sourceRadius));
static_assert(offsetof(LocalDecalGpuData, normalWeight) == offsetof(LocalLightGpuData, shapeTangent));

} // namespace HIKARI::RENDER3D::LIGHTING
