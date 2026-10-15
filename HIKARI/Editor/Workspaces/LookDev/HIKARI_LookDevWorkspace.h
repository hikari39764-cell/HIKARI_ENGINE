#pragma once

#include <optional>
#include <string>

#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"

namespace HIKARI::EDITOR {

// 布景の工作区の状態（置く物の選択と吸着）と配置。場面の物体・履歴・環境は DocumentSceneEditorController が持ち、
// ここは美術向けの操作の窓口だけを持つ。
class LookDevWorkspace {
public:
    // 視口・摆放・物体と灯光（同じ場所の札）・检查器・灯光与画面の既定の配置。
    void DrawDockSpace(bool resetLayout);

    // 置く物の一覧と吸着の設定（摆放の窓の上半分）。物を押すと置く状態になり、視口でクリックした面へ置く。
    // 同じ物をもう一度押すか Esc で終わる。
    void DrawPalette();
    // 置く状態の時、R で 90° 回す・Esc で終える（視口に焦点がある時）。
    void HandlePlacementKeys(bool viewportFocused);

    std::optional<LookDevPlaceItem> ArmedItem() const noexcept { return armed_; }
    float GridStep() const noexcept { return snap_ ? gridStep_ : 0.0f; }
    float PlacementYaw() const noexcept { return yawDegrees_; }
    // 「整理」の阵列の設定と、换成模型で選んだ模型（資産の GUID）。
    LookDevArraySettings& ArraySettings() noexcept { return arraySettings_; }
    std::string& ReplacementModel() noexcept { return replacementModel_; }
    // A/B 分屏：開いた時の画を A として左に残し、右に今の画（B）を出す。position は境目の位置（0..1）。
    bool& SplitCompare() noexcept { return splitCompare_; }
    float& SplitPosition() noexcept { return splitPosition_; }
    bool& SplitDragging() noexcept { return splitDragging_; }
    // A を撮った時の相機（視点が動くと左右が合わなくなるので、変わったら知らせる）。
    MATH::Mat4& SplitCamera() noexcept { return splitCamera_; }

private:
    std::optional<LookDevPlaceItem> armed_{};
    bool snap_ = true;
    float gridStep_ = 0.5f;
    float yawDegrees_ = 0.0f;
    LookDevArraySettings arraySettings_{};
    std::string replacementModel_{};
    bool splitCompare_ = false;
    float splitPosition_ = 0.5f;
    bool splitDragging_ = false;
    MATH::Mat4 splitCamera_{};
};

} // namespace HIKARI::EDITOR
