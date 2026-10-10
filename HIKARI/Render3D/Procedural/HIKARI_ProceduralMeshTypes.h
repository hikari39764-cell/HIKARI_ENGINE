#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

#include <json.hpp>

namespace HIKARI {

enum class ProceduralMeshKind : uint8_t {
    Plane,
    GridPlane,
    Box,
    Sphere,
    Cylinder,
    Capsule,
    // 斜面（くさび）。底は width x depth、-Z の手前の下端から +Z の奥の上端へ height だけ上がる。
    Ramp,
    // 階段。斜面と同じ向きに segmentsY 段で上がる。
    Stairs,
};

// 箱の切り抜き（局所の中心と大きさ）。床の穴、壁の門窓の開口、挖空方块で削った所。
struct ProceduralMeshCut {
    MATH::Vec3 center{};
    MATH::Vec3 size{1.0f, 1.0f, 1.0f};
    // 丸い切り抜き（円の穴・丸窓）の貫く軸（0 X、1 Y、2 Z）。-1 は四角。丸は軸に沿って箱を貫き、残り二軸は同じ直径。
    int8_t roundAxis = -1;

    bool operator==(const ProceduralMeshCut&) const = default;
};
inline constexpr size_t kMaximumProceduralMeshCuts = 16;

struct ProceduralMeshSettings {
    ProceduralMeshKind kind = ProceduralMeshKind::Box;
    float width = 1.0f;
    float height = 1.0f;
    float depth = 1.0f;
    uint32_t segmentsX = 10;
    uint32_t segmentsY = 10;
    uint32_t segmentsZ = 1;
    uint32_t sphereSlices = 32;
    uint32_t sphereStacks = 16;
    // 両面描画は cluster の cone culling を無効化するため、必要な面だけ明示的に有効化する。
    bool doubleSided = false;
    bool generateTangents = true;
    // 0 は従来どおり segment 数を UV tile 数に使う。terrain の
    // response mesh は subdivision だけ増やしても材質密度を変えないため、
    // authored grid 由来の固定 tile 数を明示する。
    float uvTilesX = 0.0f;
    float uvTilesY = 0.0f;
    // 箱だけが持つ切り抜き。描画と衝突は「箱 - 切り抜き」の残り。
    std::vector<ProceduralMeshCut> cuts{};

    bool operator==(const ProceduralMeshSettings&) const = default;
};

// 切り抜いた箱の格子：箱と切り抜きの境界で各軸を区切り、升ごとに残るかを持つ。描画の面と衝突の箱が共有する。
// 丸い切り抜きは外接する正方形の柱を升から抜き、四隅の残りと穴の壁を別に作る。
struct ProceduralCutGrid {
    std::array<std::vector<float>, 3> edges{};
    // 升を削った切り抜きの番号（-1 は残った升）。
    std::vector<int16_t> carvedBy{};

    int Count(int axis) const noexcept { return static_cast<int>(edges[static_cast<size_t>(axis)].size()) - 1; }
    // 範囲外は空き。
    bool IsSolid(int x, int y, int z) const noexcept { return CarvedBy(x, y, z) == -1; }
    // 削った切り抜きの番号。残った升は -1、範囲外は -2。
    int CarvedBy(int x, int y, int z) const noexcept;
};
ProceduralCutGrid BuildProceduralCutGrid(const ProceduralMeshSettings& settings);

// 残りを重ならない箱（局所の中心と大きさ）に分ける。切り抜きが無ければ箱そのもの一つ。丸い穴の四隅の残りは
// 円の外に収まる階段状の箱で近似する（衝突の穴は見た目より少し大きい）。
struct ProceduralMeshBox {
    MATH::Vec3 center{};
    MATH::Vec3 size{};
};
std::vector<ProceduralMeshBox> DecomposeProceduralCutBox(const ProceduralMeshSettings& settings);

nlohmann::json WriteProceduralMeshCuts(const std::vector<ProceduralMeshCut>& cuts);
std::vector<ProceduralMeshCut> ReadProceduralMeshCuts(const nlohmann::json& value);

// 斜面と階段の衝突の箱：斜面（手前の下端から奥の上端）を上面にして、その下へ厚みを取る。
void FitProceduralSlopeBox(const ProceduralMeshSettings& settings, MATH::Vec3& center, MATH::Vec3& rotationDegrees,
                           MATH::Vec3& size) noexcept;

const char* ToString(ProceduralMeshKind kind) noexcept;
const char* GetProceduralMeshDisplayName(ProceduralMeshKind kind) noexcept;
ProceduralMeshKind ParseProceduralMeshKind(const nlohmann::json& value,
                                           ProceduralMeshKind fallback = ProceduralMeshKind::Box);
ProceduralMeshSettings SanitizeProceduralMeshSettings(ProceduralMeshSettings settings) noexcept;

} // namespace HIKARI
