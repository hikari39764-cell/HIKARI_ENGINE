#pragma once

#include <cstdint>

// 材質の制御図：標本 1 つ = 4 バイト。主材質と重ね材質の 2 種類と混合度を持ち、シェーダーは 1 回の読みで足りる。
// 値 0 が既定の状態（材質 0、重ねなし、自動規則が塗ってよい、穴なし、ナビ対象）になるよう並べる。
//
//   bit  0- 4  主材質（0..31）
//   bit  5- 9  重ね材質（0..31）
//   bit 10-17  混合度（0 = 主材質だけ、255 = 重ね材質だけ）
//   bit 18-21  UV 回転（16 段、22.5 度刻み）
//   bit 22-24  UV 縮尺（8 段、kTerrainControlUvScales を参照）
//   bit 25     穴（地形の面を抜く）
//   bit 26     手塗り（自動材質規則はこの標本を書き換えない）
//   bit 27     ナビから外す
//   bit 28-31  予約（0 でなければ読み込みで拒否）
namespace HIKARI::TERRAIN::DATA {

inline constexpr uint32_t kTerrainControlMaterialCount = 32u;
inline constexpr uint32_t kTerrainControlUvRotationSteps = 16u;
inline constexpr uint32_t kTerrainControlUvScaleSteps = 8u;
inline constexpr uint32_t kTerrainControlReservedMask = 0xF0000000u;

struct ControlTexel {
    uint8_t baseMaterial = 0u;
    uint8_t overlayMaterial = 0u;
    uint8_t blend = 0u;
    uint8_t uvRotation = 0u;
    uint8_t uvScale = 0u;
    bool hole = false;
    bool handPainted = false;
    bool navigationExcluded = false;
    bool operator==(const ControlTexel&) const = default;
};

// 範囲外の値は詰めずに false を返す。
inline bool PackControlTexel(const ControlTexel& texel, uint32_t& out) noexcept {
    if (texel.baseMaterial >= kTerrainControlMaterialCount || texel.overlayMaterial >= kTerrainControlMaterialCount ||
        texel.uvRotation >= kTerrainControlUvRotationSteps || texel.uvScale >= kTerrainControlUvScaleSteps) {
        return false;
    }
    out = static_cast<uint32_t>(texel.baseMaterial) | (static_cast<uint32_t>(texel.overlayMaterial) << 5u) |
          (static_cast<uint32_t>(texel.blend) << 10u) | (static_cast<uint32_t>(texel.uvRotation) << 18u) |
          (static_cast<uint32_t>(texel.uvScale) << 22u) | (texel.hole ? 1u << 25u : 0u) |
          (texel.handPainted ? 1u << 26u : 0u) | (texel.navigationExcluded ? 1u << 27u : 0u);
    return true;
}

inline bool IsValidControlValue(uint32_t value) noexcept { return (value & kTerrainControlReservedMask) == 0u; }

inline ControlTexel UnpackControlTexel(uint32_t value) noexcept {
    ControlTexel texel{};
    texel.baseMaterial = static_cast<uint8_t>(value & 31u);
    texel.overlayMaterial = static_cast<uint8_t>((value >> 5u) & 31u);
    texel.blend = static_cast<uint8_t>((value >> 10u) & 255u);
    texel.uvRotation = static_cast<uint8_t>((value >> 18u) & 15u);
    texel.uvScale = static_cast<uint8_t>((value >> 22u) & 7u);
    texel.hole = ((value >> 25u) & 1u) != 0u;
    texel.handPainted = ((value >> 26u) & 1u) != 0u;
    texel.navigationExcluded = ((value >> 27u) & 1u) != 0u;
    return texel;
}

// UV 縮尺の段 → 倍率（段 0 が 1 倍、1..7 は 0.5, 0.67, 0.8, 1.25, 1.5, 2, 3 倍）。シェーダーも同じ表を持つ。
inline constexpr float kTerrainControlUvScales[kTerrainControlUvScaleSteps] = {1.0f,  0.5f, 0.67f, 0.8f,
                                                                               1.25f, 1.5f, 2.0f,  3.0f};

// 材質 material を重み weight（0..1）で 1 回塗る。主材質なら重ねを薄め、重ね材質なら濃くし、どちらでもなければ
// 優勢な方を主材質に残して重ねを差し替える。塗った標本は手塗りになる。
inline ControlTexel PaintControlMaterial(ControlTexel texel, uint8_t material, float weight) noexcept {
    const float w = weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
    const auto toByte = [](float value) {
        return static_cast<uint8_t>(value <= 0.0f ? 0.0f : (value >= 255.0f ? 255.0f : value + 0.5f));
    };
    if (texel.baseMaterial == material) {
        texel.blend = toByte(static_cast<float>(texel.blend) * (1.0f - w));
    } else if (texel.overlayMaterial == material && texel.blend > 0u) {
        texel.blend = toByte(static_cast<float>(texel.blend) + (255.0f - static_cast<float>(texel.blend)) * w);
    } else {
        if (texel.blend >= 128u) {
            texel.baseMaterial = texel.overlayMaterial;
        }
        texel.overlayMaterial = material;
        texel.blend = toByte(255.0f * w);
    }
    if (texel.blend == 255u) {
        texel.baseMaterial = texel.overlayMaterial;
        texel.blend = 0u;
    }
    texel.handPainted = true;
    return texel;
}

} // namespace HIKARI::TERRAIN::DATA
