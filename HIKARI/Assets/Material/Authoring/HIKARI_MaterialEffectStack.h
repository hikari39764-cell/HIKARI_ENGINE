#pragma once
#include <span>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "Render3D/HIKARI_Math3D.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {
    struct MaterialAssetData;
    enum class MaterialEffectKind : uint8_t {
        UvFlow, UvDistortion, Gradient, Fade, Wipe, Dissolve, Emission, Fresnel,
        Opacity, Glow, Blur, LightSweep, LuminanceMask,
        // AE の放射ブラーとタービュレント変位。既存のノードだけで展開する。
        RadialBlur, TurbulentDisplace,
        // 色調整。露出・コントラスト・彩度・黒レベルを四則演算だけで適用する。
        ColorCorrect,
        // 片側へ減衰しながら伸ばす拖尾。対称な Blur とは別物で、
        // 「尾を引く」見た目を作る。
        Streak,
        Pixelate, Posterize, Ink, FlowMap
    };
    enum class MaterialEffectTextureTarget : uint8_t { Shared, BaseColor, Mask, Noise, DetailNoise, FlowMap };
    std::span<const std::string_view> GetMaterialEffectTextureTargets() noexcept;
    std::string_view MaterialEffectTextureTargetName(MaterialEffectTextureTarget target) noexcept;
    bool ParseMaterialEffectTextureTarget(std::string_view name, MaterialEffectTextureTarget& target) noexcept;
    enum class MaterialEffectStage : uint8_t { Uv, Blur, Streak, RadialBlur, Surface, Composite };
    MaterialEffectStage GetMaterialEffectStage(MaterialEffectKind kind) noexcept;
    std::string_view MaterialEffectStageName(MaterialEffectStage stage) noexcept;
    bool IsCompositeMaterialEffect(MaterialEffectKind kind) noexcept;
    bool IsCompositeOnlyMaterialEffect(MaterialEffectKind kind) noexcept;
    // enum 順の UI 配列を持たず、資産の効果定義から両方の編集経路を生成する。
    std::array<const char*, 4> MaterialEffectParameterNames(MaterialEffectKind kind, bool secondary = false) noexcept;
    std::array<const char*, 4> CompositeEffectParameterNames(MaterialEffectKind kind, bool secondary = false) noexcept;
    struct MaterialEffectStep {
        std::string id;
        MaterialEffectKind kind = MaterialEffectKind::UvFlow;
        bool enabled = true;
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
    std::string_view MaterialEffectName(MaterialEffectKind) noexcept;
    // 効果名の唯一の一覧。UI の選択肢と AI 操作の説明はここから作る。
    std::span<const std::string_view> GetMaterialEffectKindNames() noexcept;
    // 多段グラデーションで許すストップ数。粒子色ストップと同じ上限を使う。
    inline constexpr size_t kMaximumMaterialEffectColorStops = 8u;
    // 選択肢を AI 操作の説明文へまとめる。手書きの一覧を残さないため。
    std::string MaterialEffectKindList();
    bool ParseMaterialEffectKind(std::string_view, MaterialEffectKind&) noexcept;
    MaterialEffectStep MakeMaterialEffect(MaterialEffectKind, std::string id);
    // 効果スタックが参照する貼図スロット。空のスロットは無効のまま扱う。
    struct MaterialEffectTextureSet {
        AssetGuid baseColor{};
        AssetGuid mask{};
        AssetGuid noise{};
        // 2 つ目のノイズ場。歪み/溶解の細部を別の貼図で与える。
        AssetGuid detailNoise{};
        AssetGuid flowMap{};
    };
    // 全て既存の材質ノードへ展開。VFX 専用の GPU インタープリターは持たない。
    MaterialAssetData BuildMaterialEffectStack(const MaterialEffectTextureSet& textures,
        std::span<const MaterialEffectStep> effects, bool lit = false,
        uint32_t atlasColumns = 1, uint32_t atlasRows = 1,
        std::span<const std::string> animatedParameters = {});
}
