#pragma once

#include <optional>

#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"

namespace HIKARI::EDITOR {

// 布景の工作区の状態（置く物の選択と吸着）と配置。場面の物体・履歴・環境は DocumentSceneEditorController が持ち、
// ここは美術向けの操作の窓口だけを持つ。
class LookDevWorkspace {
public:
    // 視口・摆放・检查器・灯光与画面の既定の配置。
    void DrawDockSpace(bool resetLayout);

    // 置く物の一覧と吸着の設定（摆放の窓の上半分）。物を押すと置く状態になり、視口でクリックした面へ置く。
    // 同じ物をもう一度押すか Esc で終わる。
    void DrawPalette();
    // 置く状態の時、R で 90° 回す・Esc で終える（視口に焦点がある時）。
    void HandlePlacementKeys(bool viewportFocused);

    std::optional<LookDevPlaceItem> ArmedItem() const noexcept { return armed_; }
    void Disarm() noexcept { armed_.reset(); }
    float GridStep() const noexcept { return snap_ ? gridStep_ : 0.0f; }
    float PlacementYaw() const noexcept { return yawDegrees_; }

private:
    std::optional<LookDevPlaceItem> armed_{};
    bool snap_ = true;
    float gridStep_ = 0.5f;
    float yawDegrees_ = 0.0f;
};

} // namespace HIKARI::EDITOR
