#pragma once

#include <cstdint>
#include <string>

#include "Core/Math/HIKARI_Math3D.h"

// 面へ投影するデカールの作者値と、描画へ渡す意味情報。シーン文書との読み書きは DecalComponent が持つ。
namespace HIKARI::RENDER3D::DECAL {

// 箱の辺 [m] の上限。
inline constexpr float kMaximumDecalExtent = 100.0f;

// 物体の原点を中心にした size の箱へ、ローカル +Z の向きに投影する。右手系で投影の向きに見て
// テクスチャの右が -X、上が +Y（鏡像にならない）。
struct DecalSettings {
    bool enabled = true;
    // 幅（ローカル X）、高さ（ローカル Y）、投影の深さ（ローカル Z）[m]。
    MATH::Vec3 size{1.0f, 1.0f, 0.5f};
    // 色と被覆（A）の Texture 資産。空は color 一色で被覆 1。
    std::string baseColorTexture;
    // 接空間の法線の Texture 資産（Linear）。空は面の法線を変えない。
    std::string normalTexture;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    // 色・法線・粗さと金属度をそれぞれどれだけ乗せるか（被覆 x opacity に掛ける）。色を 0 にすると濡れや凹凸だけを乗せる。
    float colorWeight = 1.0f;
    float normalWeight = 1.0f;
    float materialWeight = 0.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    // 面の法線と投影の逆向きの角がこの値を超えると薄れ始め、90° で消える。
    float angleFadeDegrees = 60.0f;
    // 重なりの順（大きいほど上に乗る）。
    int sortOrder = 0;

    bool operator==(const DecalSettings&) const = default;
};

struct SceneDecalProvider {
    std::string id{};
    // 物体のワールド行列（拡縮を含む）。箱は settings.size をさらに掛けた大きさ。
    MATH::Mat4 world{};
    DecalSettings settings{};
    // 色と法線の Texture 資産の描画用テクスチャの記述子番号 + 1（0 は無し）。描画の直前に LocalTextureResolver が埋める。
    uint32_t baseColorTextureIndex = 0u;
    uint32_t normalTextureIndex = 0u;
};

} // namespace HIKARI::RENDER3D::DECAL
