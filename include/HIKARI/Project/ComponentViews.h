#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/DecalTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LightTypes.h"
#include "HIKARI/Project/PresentationTypes.h"
#include "HIKARI/Project/WorldSpriteTypes.h"
#include "HIKARI/Project/WorldTextTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

class ObjectView;

// ObjectView::GetLight/GetDecal/GetModel/GetWorldSprite/GetWorldText から取得する、
// 既存の組み込みコンポーネントへの非所有ビュー。World 所有スレッドでのみ有効。
// 変更は実行時のみで Scene 文書へは保存されない（World 再構築後は再適用する）。
// 変更系はすべて GameProjectWorldResult を返し、対象コンポーネントが無い場合は Unsupported。

class LightView {
public:
    LightView() = default;
    // outState の size/version を初期化してから呼ぶ。対応外のヘッダーは false で変更しない。
    HIKARI_GAME_PROJECT_SDK_API bool TryGetState(GameProjectLightState& outState) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ApplyPatch(const GameProjectLightPatch& patch) const noexcept;

private:
    friend class ObjectView;
    explicit LightView(GameProjectObjectHandle object) noexcept : object_(object) {}
    GameProjectObjectHandle object_{};
};

class DecalView {
public:
    DecalView() = default;
    // outState の size/version を初期化してから呼ぶ。対応外のヘッダーは false で変更しない。
    HIKARI_GAME_PROJECT_SDK_API bool TryGetState(GameProjectDecalState& outState) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ApplyPatch(const GameProjectDecalPatch& patch) const noexcept;

private:
    friend class ObjectView;
    explicit DecalView(GameProjectObjectHandle object) noexcept : object_(object) {}
    GameProjectObjectHandle object_{};
};

class ModelView {
public:
    ModelView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetAppearance(GameProjectModelAppearance& outAppearance) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    ApplyAppearance(const GameProjectModelAppearance& appearance) const noexcept;
    // 材質槽 materialIndex の色を実行時だけ変える（受けた攻撃で一瞬光らせる等）。制作時の上書きが無い槽は、
    // 取り込んだ材質の写し（テクスチャはモデルのまま）を作ってから変える。資産と Scene 文書は変えない。
    // 発光は画面基準：emissiveColor × emissiveStrength の 1 が露出後の白で、昼でも夜でも同じ明るさに見える（周りは
    // 照らさない）。色は 0〜64、opacity は 0〜1 に丸める。槽が無い・モデルが読めていない時は InvalidRequest / Unsupported。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetMaterialColorOverride(uint32_t materialIndex,
                                                                                GameProjectFloat3 baseColor,
                                                                                float opacity,
                                                                                GameProjectFloat3 emissiveColor,
                                                                                float emissiveStrength) const noexcept;
    // 材質槽が今描いている色を読む。outColor の size/version を初期化してから呼ぶ。槽が無ければ false。
    HIKARI_GAME_PROJECT_SDK_API bool TryGetMaterialColor(uint32_t materialIndex,
                                                         GameProjectModelMaterialColor& outColor) const noexcept;
    // SetMaterialColorOverride の前の色へ戻す。上書きしていない槽は何もせず Success。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    ClearMaterialColorOverride(uint32_t materialIndex) const noexcept;
    // 名前が完全一致する最初のノードの描画可否。アニメーション・衝突・変換は変えない。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetNodeVisibility(std::string_view nodeName, bool visible,
                      GameProjectModelNodeVisibilityScope scope =
                          GameProjectModelNodeVisibilityScope::NodeAndDescendants) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ClearNodeVisibility(std::string_view nodeName) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ClearAllNodeVisibility() const noexcept;
    // Bits 0..30 はプロジェクト分類、bit 31 は予約。0 は自動分類。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetPostGroupMask(uint32_t mask) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ClearPostGroupMask() const noexcept;

private:
    friend class ObjectView;
    explicit ModelView(GameProjectObjectHandle object) noexcept : object_(object) {}
    GameProjectObjectHandle object_{};
};

class WorldSpriteView {
public:
    WorldSpriteView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetState(GameProjectWorldSpriteState& outState) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetTexture(std::string_view textureAssetGuid) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    ApplyPatch(const GameProjectWorldSpritePatch& patch) const noexcept;

private:
    friend class ObjectView;
    explicit WorldSpriteView(GameProjectObjectHandle object) noexcept : object_(object) {}
    GameProjectObjectHandle object_{};
};

class WorldTextView {
public:
    WorldTextView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetState(GameProjectWorldTextState& outState) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    ApplyPatch(const GameProjectWorldTextPatch& patch) const noexcept;
    // UTF-8 は呼び出し中にコピーされる。空のフォントは制作時のフォントを保つ。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetContent(std::string_view text,
                                                                  std::string_view fontAssetPath = {}) const noexcept;

private:
    friend class ObjectView;
    explicit WorldTextView(GameProjectObjectHandle object) noexcept : object_(object) {}
    GameProjectObjectHandle object_{};
};

static_assert(std::is_trivially_copyable_v<LightView>);
static_assert(std::is_trivially_copyable_v<DecalView>);
static_assert(std::is_trivially_copyable_v<ModelView>);
static_assert(std::is_trivially_copyable_v<WorldSpriteView>);
static_assert(std::is_trivially_copyable_v<WorldTextView>);

} // namespace HIKARI
