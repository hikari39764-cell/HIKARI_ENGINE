#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LightTypes.h"
#include "HIKARI/Project/PresentationTypes.h"
#include "HIKARI/Project/WorldSpriteTypes.h"
#include "HIKARI/Project/WorldTextTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

class ObjectView;

// ObjectView::GetLight/GetModel/GetWorldSprite/GetWorldText から取得する、
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

class ModelView {
public:
    ModelView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetAppearance(GameProjectModelAppearance& outAppearance) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    ApplyAppearance(const GameProjectModelAppearance& appearance) const noexcept;
    // 既存の実行時マテリアル上書きを更新する。マテリアルや元資産は作らない。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetMaterialColorOverride(uint32_t materialIndex,
                                                                                GameProjectFloat3 baseColor,
                                                                                float opacity,
                                                                                GameProjectFloat3 emissiveColor,
                                                                                float emissiveStrength) const noexcept;
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
static_assert(std::is_trivially_copyable_v<ModelView>);
static_assert(std::is_trivially_copyable_v<WorldSpriteView>);
static_assert(std::is_trivially_copyable_v<WorldTextView>);

} // namespace HIKARI
