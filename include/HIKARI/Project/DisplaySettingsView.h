#pragma once

#include "HIKARI/Project/DisplaySettingsTypes.h"
#include "HIKARI/Project/WorldView.h"

namespace HIKARI {

    // World の世代を検証する非所有ビュー。呼び出しは World の所有スレッドに限る。
    // 設定値と GPU/ウィンドウ資源はエンジンが所有し、このビューは保存を行わない。
    class DisplaySettingsView {
    public:
        DisplaySettingsView() = default;
        HIKARI_GAME_PROJECT_SDK_API bool GetSettings(GameProjectDisplaySettings& out) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetCapabilities(GameProjectDisplayCapabilities& out) const noexcept;

        // 全項目を検証してから受理する。拒否時は変更しない。Play のゲーム窓のみ適用可能。
        // GPU 同期・リサイズは次のフレーム境界で実行される。Accepted は完了通知ではない。
        // 切り替え後の有効値は GetSettings で取得する。永続化はゲームの保存 API を使う。
        HIKARI_GAME_PROJECT_SDK_API GameProjectDisplayApplyResult Apply(
            const GameProjectDisplaySettings& settings) const noexcept;

    private:
        friend class WorldView;
        explicit DisplaySettingsView(WorldView world) noexcept : world_(world) {}
        WorldView world_{};
    };
}
