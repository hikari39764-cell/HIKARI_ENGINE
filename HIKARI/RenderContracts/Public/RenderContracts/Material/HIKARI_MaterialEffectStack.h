#pragma once
#include <span>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI {
enum class MaterialEffectKind : uint8_t {
#define HIKARI_EFFECT(name, id) name = id,
#include "Shaders/Include/Vfx/HIKARI_MaterialEffectKinds.hlsli"
#undef HIKARI_EFFECT
};
enum class MaterialEffectTextureTarget : uint8_t { Shared, BaseColor, Mask, Noise, DetailNoise, FlowMap };
enum class MaterialEffectMask : uint8_t { None, Radial, Horizontal, Vertical, Alpha, Luminance };
std::span<const std::string_view> GetMaterialEffectTextureTargets() noexcept;
std::span<const std::string_view> GetMaterialEffectTextureTargets(MaterialEffectKind kind) noexcept;
std::string_view MaterialEffectTextureTargetName(MaterialEffectTextureTarget target) noexcept;
bool ParseMaterialEffectTextureTarget(std::string_view name, MaterialEffectTextureTarget& target) noexcept;
enum class MaterialEffectStage : uint8_t { Uv, Blur, Streak, RadialBlur, Surface };
MaterialEffectStage GetMaterialEffectStage(MaterialEffectKind kind) noexcept;
std::string_view MaterialEffectStageName(MaterialEffectStage stage) noexcept;
bool IsAdjustmentMaterialEffect(MaterialEffectKind kind) noexcept;
bool SupportsSpatialMaterialEffect(MaterialEffectKind kind) noexcept;
enum class MaterialEffectSpace : uint8_t { LayerUv, Group3D };
inline constexpr TEXT::EnumName<MaterialEffectSpace> kMaterialEffectSpaceNames[]{
    {MaterialEffectSpace::LayerUv, "LayerUv"},
    {MaterialEffectSpace::Group3D, "Group3D"},
};
// enum 順の UI 配列を持たず、資産の効果定義から両方の編集経路を生成する。
enum class MaterialEffectCategory : uint8_t { Mapping, Mask, Color, Light, Filter, Style, Count };
MaterialEffectCategory GetMaterialEffectCategory(MaterialEffectKind kind) noexcept;
std::string_view MaterialEffectCategoryName(MaterialEffectCategory category) noexcept;
struct MaterialEffectParameterInfo {
    const char* name = nullptr;
    float minimum = 0;
    float maximum = 1;
    bool animatable = false;
};
struct MaterialEffectStep;
std::array<MaterialEffectParameterInfo, 4> GetMaterialEffectParameters(const MaterialEffectStep& effect,
                                                                       bool secondary = false) noexcept;
std::span<const std::string_view> GetMaterialEffectMasks(MaterialEffectSpace space) noexcept;
bool MaterialEffectUsesColor(MaterialEffectKind kind, bool secondary = false) noexcept;
struct MaterialEffectStep {
    std::string id;
    MaterialEffectKind kind = MaterialEffectKind::UvFlow;
    bool enabled = true;
    MaterialEffectSpace space = MaterialEffectSpace::LayerUv;
    float mix = 1.0f;
    MaterialEffectMask mask = MaterialEffectMask::None;
    bool maskInvert = false;
    // ローカル UV の中心、半径（線形では境界）、ぼかし。
    MATH::Vec4 maskParameters{0.5f, 0.5f, 0.5f, 0.1f};
    MaterialEffectTextureTarget textureTarget = MaterialEffectTextureTarget::Shared;
    MATH::Vec4 parameters{};
    // セカンダリパラメータ。UV 回転・軸交換など、parameters の 4 枠で
    // 足りない効果が使う。未使用の効果では 0 のまま。
    MATH::Vec4 parameters2{};
    MATH::Vec4 colorA{1, 1, 1, 1};
    MATH::Vec4 colorB{1, 1, 1, 1};
    // 多段グラデーションのストップ。空なら colorA/colorB の 2 色で動く。
    // id は編集対象として安定に指すために持つ。
    struct ColorStop {
        std::string id{};
        float time = 0.0f;
        MATH::Vec4 color{1, 1, 1, 1};
    };
    std::vector<ColorStop> colorStops{};
};
// 調整レイヤーでは変形だけを UV/幾何へ送り、風格は合成結果へ適用する。
bool IsCompositeAdjustmentEffect(const MaterialEffectStep& effect) noexcept;
std::string_view MaterialEffectName(MaterialEffectKind) noexcept;
// 空なら有効。コンパイル前後で共通する値の契約だけを検査する。
std::string_view ValidateMaterialEffect(const MaterialEffectStep&) noexcept;
// 効果名の唯一の一覧。UI の選択肢と AI 操作の説明はここから作る。
std::span<const std::string_view> GetMaterialEffectKindNames() noexcept;
// 多段グラデーションで許すストップ数。粒子色ストップと同じ上限を使う。
inline constexpr size_t kMaximumMaterialEffectColorStops = 8u;
// 選択肢を AI 操作の説明文へまとめる。手書きの一覧を残さないため。
std::string MaterialEffectKindList();
bool ParseMaterialEffectKind(std::string_view, MaterialEffectKind&) noexcept;
MaterialEffectStep MakeMaterialEffect(MaterialEffectKind, std::string id);
} // namespace HIKARI
