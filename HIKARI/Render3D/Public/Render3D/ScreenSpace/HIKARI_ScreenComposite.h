#pragma once

#include <array>
#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"
#include "RenderCore/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D {

// 場景キャプチャを不透明に貼った平らな画面を、時間方向の拡大（DLSS・TAA）の後に出力の解像度で貼り直す。
// 画面の中身は面と別に動く（中の人物、窓の視差）ので、面の動きベクトルで履歴を引くと残像になり、履歴を捨てると低い
// 描画解像度のまま揺れる。貼り直せば中身は拡大を通らず、キャプチャの解像度のまま毎フレーム正しい位置に出る。
inline constexpr uint32_t kScreenCompositeCandidateCount = 128u;
// 一度に貼り直す画面（大きく見える順）と、同時に使うキャプチャの貼図の上限。shader の定数と同じ値。
inline constexpr uint32_t kScreenCompositeRegionCount = 16u;
inline constexpr uint32_t kScreenCompositeTextureCount = 8u;

// 画面一枚の世界の四隅と、各隅のキャプチャ上の UV（材質の UV 変換を通した後）。四隅の並びは面の (s, t) が
// (0,0)、(1,0)、(1,1)、(0,1) の順。
struct ScreenCompositeWorldRegion {
    std::array<MATH::Vec3, 4> corners{};
    std::array<MATH::Vec2, 4> uv{};
    TextureResourceHandle texture{};
    MATH::Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ScreenCompositeWorldFrame {
    uint32_t count = 0u;
    std::array<ScreenCompositeWorldRegion, kScreenCompositeCandidateCount> regions{};
};

// shader の ScreenCompositeRegion と同じ並び（16 byte 単位）。
struct ScreenCompositeRegionConstants {
    // 出力の uv（x, y, 1）から面の (s·w, t·w, w) への射影。
    MATH::Vec4 surfaceFromScreen[3]{};
    MATH::Vec4 uv01{};
    MATH::Vec4 uv23{};
    // 深度 = x·u + y·v + z（平面の深度は画面上で一次）。w は貼図の番号。
    MATH::Vec4 depthPlane{};
    // 世界の位置 = origin + s·S + t·T（霧に使う）。
    MATH::Vec4 worldOrigin{};
    MATH::Vec4 worldS{};
    MATH::Vec4 worldT{};
    // 出力の uv での外接矩形（min.xy, max.xy）。
    MATH::Vec4 rect{};
    MATH::Vec4 baseColor{};
};
static_assert(sizeof(ScreenCompositeRegionConstants) == 176u);

// このフレームに貼り直す画面（投影済み）と、使う貼図。
struct ScreenCompositeFrame {
    uint32_t count = 0u;
    uint32_t textureCount = 0u;
    MATH::Vec3 cameraPosition{};
    std::array<ScreenCompositeRegionConstants, kScreenCompositeRegionCount> regions{};
    std::array<TextureResourceHandle, kScreenCompositeTextureCount> textures{};
};

// 世界の画面をこのカメラ（ずらしの無い投影）で出力の uv へ投影し、大きく見える順に上限まで選ぶ。
ScreenCompositeFrame ProjectScreenComposites(const ScreenCompositeWorldFrame& source, const MATH::Mat4& viewProjection,
                                             const MATH::Vec3& cameraPosition);

} // namespace HIKARI::RENDER3D
