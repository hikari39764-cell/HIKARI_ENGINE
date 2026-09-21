#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include <algorithm>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
void VfxWorkspaceController::DrawTimelineLayers(float originX, float width, float labelWidth,
                                                float duration) {
#if defined(HIKARI_WITH_EDITOR)
    auto &state = timelineEdit_;
    const auto text = [](const char *key) { return TEXT::Localize(key); };
    if (IconToggleButton(EditorGlyph::Snap, "FrameSnap", state.snap, {26,26},
            text("ui.vfx.timeline.snap").c_str())) state.snap = !state.snap;
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Help, "TimelineHelp", EditorButtonTone::Quiet,
            {26,26}, text("ui.vfx.timeline.keyHelp").c_str())) ImGui::OpenPopup("TimelineHelp");
    if (ImGui::BeginPopup("TimelineHelp")) {
        ImGui::TextUnformatted(text("ui.vfx.timeline.keyHelp").c_str());
        ImGui::TextUnformatted(text("ui.vfx.timeline.layerHelp").c_str());
        ImGui::EndPopup();
    }
    const auto top = ImGui::GetCursorScreenPos();
    const auto mouse = ImGui::GetIO().MousePos;
    const auto &io = ImGui::GetIO();
    auto &draw = *ImGui::GetWindowDrawList();
    const auto toX = [&](float seconds) { return originX + seconds / duration * width; };
    const auto selected = [&](const std::string &id) {
        return std::find(state.selectedKeys.begin(), state.selectedKeys.end(), id) !=
               state.selectedKeys.end();
    };
    struct Mark {
        ImVec2 point;
        std::vector<std::string> ids;
    };
    std::vector<Mark> marks;
    bool hit = false;
    const bool canStart = !state.baseline && !state.suppressDragUntilRelease;
    const auto snapshot = document_.Asset();
    for (const auto* orderedLayer : VFX::CollectVfxLayerTree(snapshot)) {
        const auto& layer = *orderedLayer;
        ImGui::PushID(layer.id.c_str());
        const bool current = selection_.emitterId == layer.id;
        auto row = ImGui::GetCursorScreenPos();
        const std::string label = (layer.kind == VFX::VfxLayerKind::Adjustment ? "[FX] " : "") + layer.name;
        if (ImGui::Selectable(label.c_str(), current, 0, {labelWidth - 8, 22}))
            Select({VfxWorkspaceSelectionKind::Emitter, layer.id, layer.id});
        const float span =
            layer.activeDuration > 0 ? layer.activeDuration : snapshot.durationSeconds - layer.startDelay;
        const float startX = toX(layer.startDelay), endX = toX(layer.startDelay + span);
        draw.AddRectFilled({startX, row.y + 3}, {endX, row.y + 19},
                           layer.kind == VFX::VfxLayerKind::Adjustment ? IM_COL32(126, 82, 145, 220) :
                           layer.kind == VFX::VfxLayerKind::Particles ? IM_COL32(110, 83, 44, 210)
                                                                      : IM_COL32(39, 99, 117, 210),
                           3);
        draw.AddLine({startX, row.y + 3}, {startX, row.y + 19}, IM_COL32(143, 196, 209, 240), 3);
        draw.AddLine({endX, row.y + 3}, {endX, row.y + 19}, IM_COL32(143, 196, 209, 240), 3);
        ImGui::SetCursorScreenPos({originX, row.y});
        ImGui::InvisibleButton("##layer", {width, 22});
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", text("ui.vfx.timeline.layerHelp").c_str());
            if (canStart && ImGui::IsMouseClicked(0) && mouse.x >= startX - 6 && mouse.x <= endX + 6) {
                Select({VfxWorkspaceSelectionKind::Emitter, layer.id, layer.id});
                BeginTimelineEdit();
                state.draggedLayer = layer.id;
                state.dragMouseX = mouse.x;
                state.layerMode = std::abs(mouse.x - startX) <= 6 ? VfxLayerTimeEdit::TrimStart
                                  : std::abs(mouse.x - endX) <= 6
                                      ? (io.KeyAlt ? VfxLayerTimeEdit::Retime : VfxLayerTimeEdit::TrimEnd)
                                      : VfxLayerTimeEdit::Move;
                hit = true;
            }
        }
        for (const auto &key : layer.spawn.burstTimeline)
            draw.AddCircleFilled({toX(layer.startDelay + key.time), row.y + 11}, 4,
                                 IM_COL32(255, 190, 87, 255));
        if (layer.kind == VFX::VfxLayerKind::Particles && ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(VfxUiText("Add Burst Key at Playhead").c_str())) {
                std::string id;
                (void)AddBurstKey(layer.id, std::clamp(previewElapsedSeconds_ - layer.startDelay, 0.0f, span),
                                  1, {}, id, statusMessage_);
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos({row.x, row.y + 24});
        if (current) {
            const auto curves = CollectVfxTimelineCurves(layer, true);
            for (size_t i = 0; i < curves.size();) {
                size_t end = i + 1;
                while (end < curves.size() && curves[end].address.moduleId == curves[i].address.moduleId &&
                       curves[end].address.propertyId == curves[i].address.propertyId)
                    ++end;
                ImGui::PushID(curves[i].address.moduleId.c_str());
                ImGui::PushID(curves[i].address.propertyId.c_str());
                row = ImGui::GetCursorScreenPos();
                const auto curveLabel = layer.kind == VFX::VfxLayerKind::Adjustment ?
                    TEXT::Localize("ui.vfx.effect.param." + curves[i].label) : VfxUiText(curves[i].label);
                ImGui::TextDisabled("%s", curveLabel.c_str());
                draw.AddLine({originX, row.y + 10}, {originX + width, row.y + 10}, IM_COL32(63, 73, 82, 255));
                std::vector<float> times;
                for (size_t c = i; c < end; ++c)
                    for (const auto &key : ResolveVfxCurve(layer, curves[c].address)->keys)
                        times.push_back(key.time);
                std::sort(times.begin(), times.end());
                times.erase(std::unique(times.begin(), times.end()), times.end());
                for (float time : times) {
                    const float local = layer.animationTiming.LayerSeconds(time, span);
                    if (local < -0.00001f || local > span + 0.00001f)
                        continue;
                    Mark mark{
                        {toX(layer.startDelay + layer.animationTiming.LayerSeconds(time, span)), row.y + 10},
                        {}};
                    for (size_t c = i; c < end; ++c)
                        for (const auto &key : ResolveVfxCurve(layer, curves[c].address)->keys)
                            if (std::abs(key.time - time) < 0.00001f)
                                mark.ids.push_back(key.id);
                    const bool active = std::any_of(mark.ids.begin(), mark.ids.end(), selected);
                    const float x = mark.point.x, y = mark.point.y;
                    draw.AddQuadFilled({x, y - 6}, {x + 6, y}, {x, y + 6}, {x - 6, y},
                                       active ? IM_COL32(255, 210, 92, 255) : IM_COL32(103, 217, 233, 255));
                    ImGui::PushID(mark.ids.front().c_str());
                    ImGui::SetCursorScreenPos({x - 8, y - 10});
                    ImGui::InvisibleButton("##key", {16, 20});
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TEXT::Localize("editor.ui.3f.s.88c94e").c_str(),
                                          layer.startDelay + layer.animationTiming.LayerSeconds(time, span));
                        if (canStart && ImGui::IsMouseClicked(0)) {
                            if (!active && !io.KeyCtrl && !io.KeyShift)
                                state.selectedKeys.clear();
                            for (const auto &id : mark.ids) {
                                if (active && io.KeyCtrl)
                                    std::erase(state.selectedKeys, id);
                                else if (!selected(id))
                                    state.selectedKeys.push_back(id);
                            }
                            if (!(active && io.KeyCtrl)) {
                                BeginTimelineEdit();
                                state.keyDragging = true;
                                state.dragMouseX = mouse.x;
                                state.dragAnchorSeconds =
                                    layer.startDelay + layer.animationTiming.LayerSeconds(time, span);
                                state.duplicate = io.KeyAlt;
                                if (state.duplicate)
                                    CopyTimelineSelection();
                                (void)SeekPreview(state.dragAnchorSeconds, statusMessage_);
                            }
                            hit = true;
                        }
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            Select({VfxWorkspaceSelectionKind::CurveKey, mark.ids.front(), layer.id,
                                    curves[i].address.moduleId, curves[i].address.propertyId,
                                    curves[i].address.channel});
                            curveEditorModuleId_ = curves[i].address.moduleId;
                            curveEditorPropertyId_ = curves[i].address.propertyId;
                            curveEditorChannel_ = curves[i].address.channel;
                        }
                    }
                    marks.push_back(std::move(mark));
                    ImGui::PopID();
                }
                ImGui::SetCursorScreenPos({row.x, row.y + 23});
                ImGui::Dummy({1, 1});
                ImGui::PopID();
                ImGui::PopID();
                i = end;
            }
        }
        ImGui::PopID();
    }
    const float bottom = ImGui::GetCursorScreenPos().y;
    draw.AddLine({toX(previewElapsedSeconds_), top.y}, {toX(previewElapsedSeconds_), bottom},
                 IM_COL32(255, 210, 92, 200));
    const bool panelHovered = ImGui::IsWindowHovered() && mouse.x >= originX && mouse.x <= originX + width &&
                              mouse.y >= top.y && mouse.y <= bottom;
    if (canStart && !hit && panelHovered && ImGui::IsMouseClicked(0)) {
        state.marquee = true;
        state.marqueeX = mouse.x;
        state.marqueeY = mouse.y;
        if (!io.KeyCtrl && !io.KeyShift)
            state.selectedKeys.clear();
        state.baselineSelection = state.selectedKeys;
    }
    if (state.marquee) {
        const ImVec2 a{(std::min)(mouse.x, state.marqueeX), (std::min)(mouse.y, state.marqueeY)};
        const ImVec2 b{(std::max)(mouse.x, state.marqueeX), (std::max)(mouse.y, state.marqueeY)};
        draw.AddRectFilled(a, b, IM_COL32(70, 160, 210, 35));
        draw.AddRect(a, b, IM_COL32(90, 190, 230, 200));
        state.selectedKeys = state.baselineSelection;
        for (const auto &mark : marks)
            if (mark.point.x >= a.x && mark.point.x <= b.x && mark.point.y >= a.y && mark.point.y <= b.y)
                for (const auto &id : mark.ids)
                    if (!selected(id))
                        state.selectedKeys.push_back(id);
        if (!ImGui::IsMouseDown(0))
            state.marquee = false;
    }
    if (state.baseline && (state.keyDragging || !state.draggedLayer.empty())) {
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Escape, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
            EndTimelineEdit(true);
            state.suppressDragUntilRelease = true;
        } else if (ImGui::IsMouseDragging(0)) {
            float delta = (mouse.x - state.dragMouseX) / width * duration;
            if (state.snap && !io.KeyShift)
                delta = std::round(delta * 60) / 60;
            if (state.keyDragging) {
                if (state.duplicate) {
                    const auto original = CopyVfxTimelineKeys(*state.baseline, state.baselineSelection);
                    if (!original.empty()) {
                        float first = state.dragAnchorSeconds;
                        for (const auto &layer : state.baseline->emitters)
                            for (const auto &track : CollectVfxTimelineCurves(layer, true))
                                for (const auto &key : ResolveVfxCurve(layer, track.address)->keys)
                                    if (std::find(state.baselineSelection.begin(),
                                                  state.baselineSelection.end(),
                                                  key.id) != state.baselineSelection.end())
                                        first = (std::min)(
                                            first, layer.startDelay +
                                                       layer.animationTiming.LayerSeconds(
                                                           key.time, layer.activeDuration > 0
                                                                         ? layer.activeDuration
                                                                         : duration - layer.startDelay));
                        (void)ApplyAsset(*state.baseline, statusMessage_, VfxTimelineEditState::mergeGroup);
                        if (std::abs(delta) > 0.0001f)
                            (void)PasteTimelineSelection(first + delta, true);
                    }
                } else
                    (void)MoveTimelineSelection(delta, true);
            } else {
                auto next = *state.baseline;
                if (EditVfxLayerTime(next, state.draggedLayer, state.layerMode, delta, statusMessage_))
                    (void)ApplyAsset(std::move(next), statusMessage_, VfxTimelineEditState::mergeGroup);
            }
        }
        if (!ImGui::IsMouseDown(0))
            EndTimelineEdit(false);
    }
    if (!ImGui::IsMouseDown(0))
        state.suppressDragUntilRelease = false;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        !state.baseline) {
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_C, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false))
            CopyTimelineSelection();
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_V, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false))
            (void)PasteTimelineSelection(previewElapsedSeconds_);
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false))
            {
                if (!timelineEdit_.selectedKeys.empty()) (void)DeleteTimelineSelection();
                else if (!selection_.emitterId.empty()) (void)RemoveEmitter(selection_.emitterId, statusMessage_);
            }
        const float nudge = (io.KeyShift ? 10.0f : 1.0f) / 60;
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_LeftArrow, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), true))
            (void)MoveTimelineSelection(-nudge);
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_RightArrow, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), true))
            (void)MoveTimelineSelection(nudge);
    }
#else
    (void)originX;
    (void)width;
    (void)labelWidth;
    (void)duration;
#endif
}
} // namespace HIKARI::EDITOR
