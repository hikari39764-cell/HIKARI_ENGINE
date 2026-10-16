#pragma once

#include "Editor/Authoring/HIKARI_CurveKeyEditing.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
struct ScalarCurveView {
    CurveKeyEditing editing;
    std::string activeChannel;
    bool fitted = false, fitSelection = false, speedGraph = false, snap = true;
    bool overlay = false, allowStationOffsets = false;
    float low = -1, high = 1, duration = 1, timeStart = 0, fps = 60;
    int axis = 0; // 0: 両方、1: 時間だけ、2: 値だけ。
    enum class Gesture { None, Keys, Box, InHandle, OutHandle, ScaleLeft, ScaleRight };
    Gesture gesture = Gesture::None;
    ImVec2 mouseStart{};
    CurveKeyRef grabbed;
    float grabbedTime = 0, rangeStart = 0, rangeEnd = 1;
    std::vector<CurveKeyRef> boxBefore;
    bool cancelled = false;
    bool handleWeight = true;
    std::optional<float> seekTime;
};
struct CurveCanvasLayout {
    ImVec2 size{};
    bool lanes = false;
    float labelWidth = 0, rowHeight = 28;
    float timeStart = 0, pixelsPerSecond = 80;
};
// 選択・キー操作はどちらの描画でも同じ editing を使う。永続化と Undo は呼び出し元文書が所有する。
bool DrawCurveEditorToolbar(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view, bool graph);
bool DrawCurveEditorCanvas(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view,
                           const CurveCanvasLayout& layout);
bool CurveEditorShortcuts(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view, bool focused);

// UI アニメーションも共通グラフを使う。
bool DrawScalarCurve(HIKARI::ScalarCurve& parameter, float duration, float playhead, ScalarCurveView& view,
                     float height = 180);
} // namespace HIKARI::EDITOR
