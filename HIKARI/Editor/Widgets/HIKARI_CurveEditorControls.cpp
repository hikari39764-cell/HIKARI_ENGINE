#include "Editor/Widgets/HIKARI_ScalarCurveEditor.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {
bool CurveEditorShortcuts(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view, bool focused) {
    auto& edit = view.editing;
    const auto& io = ImGui::GetIO();
    const auto key = [&](ImGuiKey code, bool repeat = false) {
        return ConsumeEditorShortcut(EditorShortcutScope::Editing, code, focused && !io.WantTextInput, repeat);
    };
    bool changed = false;
    // Escape はこのキャンバスのドラッグ中にも届く必要がある。通常の編集ショートカットはドラッグ中禁止。
    const bool cancelDrag = edit.Active() && focused && !io.WantTextInput &&
                            ConsumeEditorShortcut(EditorShortcutScope::Document, ImGuiKey_Escape);
    if (cancelDrag || key(ImGuiKey_Escape)) {
        changed |= edit.Cancel(channels);
        view.gesture = ScalarCurveView::Gesture::None;
        view.cancelled = true;
    }
    if (io.KeyCtrl && key(ImGuiKey_A))
        edit.SelectAll(channels);
    if (io.KeyCtrl && key(ImGuiKey_C))
        edit.Copy(channels);
    if (io.KeyCtrl && key(ImGuiKey_V))
        changed |= io.KeyShift ? edit.PasteEasing(channels) : edit.Paste(channels, playhead, view.activeChannel);
    if (io.KeyCtrl && !edit.selection.empty() && key(ImGuiKey_D)) {
        edit.Copy(channels);
        float end = playhead;
        for (const auto& item : edit.clipboard)
            end = (std::max)(end, item.sequenceTime + 1.f / view.fps);
        changed |= edit.Paste(channels, end, view.activeChannel);
    }
    if (!edit.selection.empty() && key(ImGuiKey_Delete))
        changed |= edit.Delete(channels);
    if (!edit.selection.empty() && key(ImGuiKey_F9)) {
        const auto preset = io.KeyCtrl && io.KeyShift ? CurveKeyPreset::EaseOut
                            : io.KeyShift             ? CurveKeyPreset::EaseIn
                                                      : CurveKeyPreset::Ease;
        changed |= edit.ApplyPreset(channels, preset);
        if (changed)
            view.fitted = false;
    }
    if (!edit.selection.empty() && CanUseEditorShortcut(EditorShortcutScope::Editing, focused)) {
        const ImGuiKeyChord modifiers = ImGuiMod_Alt | (io.KeyShift ? ImGuiMod_Shift : 0);
        const int direction = ImGui::Shortcut(modifiers | ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat)    ? -1
                              : ImGui::Shortcut(modifiers | ImGuiKey_RightArrow, ImGuiInputFlags_Repeat) ? 1
                                                                                                         : 0;
        if (direction) {
            edit.Begin(channels);
            changed |= edit.Transform(channels, direction * (io.KeyShift ? 10.f : 1.f) / view.fps);
            edit.End();
        }
    }
    return changed;
}
bool DrawCurveEditorToolbar(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view, bool graph) {
    bool changed = false;
    auto& edit = view.editing;
    edit.Prune(channels);
    ImGui::BeginDisabled(edit.selection.empty());
    if (ImGui::Button("插值 / 缓动"))
        ImGui::OpenPopup("curvePresets");
    if (ImGui::BeginPopup("curvePresets")) {
        for (const auto& preset : kCurveKeyPresetNames)
            if (ImGui::MenuItem(preset.name))
                changed |= edit.ApplyPreset(channels, preset.value);
        ImGui::Separator();
        if (ImGui::MenuItem("复制关键帧", "Ctrl+C"))
            edit.Copy(channels);
        if (ImGui::MenuItem("复制缓动"))
            edit.Copy(channels);
        if (ImGui::MenuItem("粘贴缓动", "Ctrl+Shift+V", false, edit.easing.has_value()))
            changed |= edit.PasteEasing(channels);
        if (ImGui::MenuItem("删除关键帧", "Delete"))
            changed |= edit.Delete(channels);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("缓动 F9"))
        changed |= edit.ApplyPreset(channels, CurveKeyPreset::Ease);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("粘贴"))
        changed |= edit.Paste(channels, playhead, view.activeChannel);
    if (graph) {
        ImGui::SameLine();
        if (ImGui::Checkbox("速度图", &view.speedGraph))
            view.fitted = false;
        ImGui::SameLine();
        if (ImGui::Button("适应全部")) {
            view.fitted = false;
            view.fitSelection = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("适应选中")) {
            view.fitted = false;
            view.fitSelection = true;
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("吸附", &view.snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(95);
    ImGui::Combo("##curveAxis", &view.axis, "自由拖动\0仅时间\0仅数值\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(65);
    ImGui::DragFloat("fps", &view.fps, 1, 1, 240, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled("已选 %zu · Shift 加选 · 拖框框选 · Alt 关闭吸附 · Alt+方向键逐帧移动", edit.selection.size());
    // 数値入力もグラフと同じ変換を使い、未選択キーを移動しない。
    if (changed && graph)
        view.fitted = false;
    if (edit.selection.empty())
        return changed;
    const auto first = edit.selection.front();
    const auto c =
        std::find_if(channels.begin(), channels.end(), [&](const auto& item) { return item.id == first.channel; });
    if (c == channels.end() || first.index >= c->curve->keys.size())
        return changed;
    const auto resolved = ResolvedScalarKey(c->curve->keys, first.index);
    float time = resolved.time + c->offset;
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputFloat("时间 (s)", &time, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        edit.Begin(channels);
        changed |= edit.Transform(channels, time - resolved.time - c->offset);
        edit.End();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", c->label.c_str(), CurveKeyEditing::Locked(*c, first.index) ? " · 机位/端点已锁定" : "");
    if (edit.selection.size() != 1)
        return changed;
    ImGui::SameLine();
    float value = resolved.value * c->valueScale + c->valueOffset;
    ImGui::SetNextItemWidth(100);
    ImGui::BeginDisabled(CurveKeyEditing::Locked(*c, first.index));
    if (ImGui::InputFloat("数值", &value, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        edit.Begin(channels);
        changed |= edit.Transform(channels, 0, value - resolved.value * c->valueScale - c->valueOffset);
        edit.End();
    }
    ImGui::EndDisabled();
    for (const bool incoming : {true, false}) {
        if ((incoming && !first.index) || (!incoming && first.index + 1 == c->curve->keys.size()))
            continue;
        float slope = (incoming ? resolved.inSlope : resolved.outSlope) * c->valueScale;
        float influence = (incoming ? resolved.shape.inWeight : resolved.shape.outWeight) * 100;
        ImGui::PushID(incoming ? "in" : "out");
        ImGui::SetNextItemWidth(100);
        const bool slopeChanged = ImGui::InputFloat(incoming ? "进入速度" : "离开速度", &slope, 0, 0, "%.3f",
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        const bool weightChanged =
            ImGui::InputFloat("影响 %", &influence, 0, 0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue);
        if (slopeChanged || weightChanged)
            changed |= CurveKeyEditing::SetHandle(*c, first.index, incoming, slope / c->valueScale, influence / 100);
        ImGui::PopID();
    }
    return changed;
}
bool DrawScalarCurve(ScalarCurve& curve, float duration, float playhead, ScalarCurveView& view, float height) {
    CurveEditChannel channel{"value", "数值", &curve, 0, duration};
    std::span channels{&channel, size_t{1}};
    view.activeChannel = channel.id;
    bool changed = DrawCurveEditorToolbar(channels, playhead, view, true);
    changed |= DrawCurveEditorCanvas(channels, playhead, view, {{ImGui::GetContentRegionAvail().x, height}});
    return changed;
}
} // namespace HIKARI::EDITOR
