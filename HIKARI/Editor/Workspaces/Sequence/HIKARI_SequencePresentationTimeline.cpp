#include "Editor/Workspaces/Sequence/HIKARI_SequencePresentationPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "Editor/Workspaces/Sequence/HIKARI_SequenceBindingPanel.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {

#if defined(HIKARI_WITH_EDITOR)
constexpr float kTrackLabelWidth = 150.0f;
constexpr float kTrackHeight = 34.0f;
constexpr float kRulerHeight = 28.0f;

ImU32 TrackColor(SequencePresentationItemKind kind, bool selected) noexcept {
    const int boost = selected ? 34 : 0;
    switch (kind) {
    case SequencePresentationItemKind::ActorAnimation:
        return IM_COL32(74 + boost, 154 + boost / 2, 232, 255);
    case SequencePresentationItemKind::Vfx:
        return IM_COL32(194 + boost / 2, 92 + boost, 221, 255);
    case SequencePresentationItemKind::TimeDilation:
        return IM_COL32(239, 169 + boost / 2, 71 + boost, 255);
    case SequencePresentationItemKind::Signal:
        return IM_COL32(83 + boost, 204, 151 + boost / 2, 255);
    case SequencePresentationItemKind::None:
    default:
        return IM_COL32(105, 116, 134, 255);
    }
}

template <class T>
T* FindById(std::vector<T>& values, uint64_t id) noexcept {
    const auto found = std::find_if(
        values.begin(), values.end(),
        [id](const T& value) { return value.id == id; });
    return found != values.end() ? &*found : nullptr;
}

#endif

} // namespace

SequencePresentationPanelResult SequencePresentationPanel::DrawTimeline(
    DocumentSceneBase& scene,
    CinematicSequence& sequence,
    const AssetGuid& sequenceAssetGuid,
    SEQUENCER::SequenceBindingContext& previewBindings,
    SceneObjectId selectedObjectId,
    float playheadSeconds,
    bool playing,
    bool editingAllowed,
    bool portableAsset) {

    SequencePresentationPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
    (void)sequenceAssetGuid;
    NormalizeSelection(sequence);

    ImGui::PushID("SequencePresentationTimeline");
    ImGui::TextDisabled("STAGE TRACKS");
    ImGui::SameLine();
    ImGui::Text("%s", sequence.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %5.3f / %5.3f s", playheadSeconds,
                        sequence.durationSeconds);

    if (!editingAllowed) {
        ImGui::BeginDisabled();
    }
    bool trackEnabled = sequence.actorAnimationTrack.enabled;
    if (ImGui::Checkbox("Actor", &trackEnabled)) {
        sequence.actorAnimationTrack.enabled = trackEnabled;
        result.sequenceChanged = true;
    }
    ImGui::SameLine();
    trackEnabled = sequence.vfxTrack.enabled;
    if (ImGui::Checkbox("VFX", &trackEnabled)) {
        sequence.vfxTrack.enabled = trackEnabled;
        result.sequenceChanged = true;
    }
    ImGui::SameLine();
    trackEnabled = sequence.timeDilationTrack.enabled;
    if (ImGui::Checkbox("Time", &trackEnabled)) {
        sequence.timeDilationTrack.enabled = trackEnabled;
        result.sequenceChanged = true;
    }
    ImGui::SameLine();
    trackEnabled = sequence.signalTrack.enabled;
    if (ImGui::Checkbox("Signals", &trackEnabled)) {
        sequence.signalTrack.enabled = trackEnabled;
        result.sequenceChanged = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(82.0f);
    ImGui::SliderInt("FPS", &snapFramesPerSecond_, 12, 120);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("Zoom", &pixelsPerSecond_, 36.0f, 420.0f,
                       "%.0f px/s", ImGuiSliderFlags_Logarithmic);
    if (!editingAllowed) {
        ImGui::EndDisabled();
    }

    const bool selectedActorValid =
        selectedObjectId.value != 0 && HasAnimator(scene, selectedObjectId);
    if (!editingAllowed || !selectedActorValid) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Use Selected Actor")) {
        const size_t bindingCountBefore = sequence.bindings.size();
        selectedBindingId_ = ResolveActorBinding(
            scene, sequence, selectedObjectId, portableAsset, previewBindings);
        result.sequenceChanged |=
            sequence.bindings.size() != bindingCountBefore;
    }
    if (!editingAllowed || !selectedActorValid) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            selectedActorValid
                ? "Create or select an actor binding for the scene selection"
                : "Select a scene object with AnimatorComponent first");
    }
    ImGui::SameLine();
    const SEQUENCER::SequenceBinding* selectedBinding =
        SEQUENCER::FindSequenceBinding(sequence.bindings, selectedBindingId_);
    const char* bindingPreview = selectedBinding != nullptr
        ? selectedBinding->name.c_str()
        : "Choose actor binding";
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::BeginCombo("##StageTarget", bindingPreview)) {
        for (const SEQUENCER::SequenceBinding& binding : sequence.bindings) {
            SceneObjectId resolved{};
            if (!SEQUENCER::ResolveSceneObjectBinding(
                    sequence.bindings, binding.id, previewBindings, resolved) ||
                !HasAnimator(scene, resolved)) {
                continue;
            }
            const bool selected = binding.id == selectedBindingId_;
            if (ImGui::Selectable(binding.name.c_str(), selected)) {
                selectedBindingId_ = binding.id;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (!editingAllowed) ImGui::BeginDisabled();
    ImGui::SameLine();
    if (ImGui::Button("+ Animation")) {
        if (selectedBindingId_.IsValid()) {
            SEQUENCER::ActorAnimationClip clip{};
            clip.id = SEQUENCER::AllocateActorAnimationClipId(
                sequence.actorAnimationTrack);
            clip.actorBindingId = selectedBindingId_;
            clip.clipName = "AnimationClip";
            clip.startTimeSeconds = SnapTime(playheadSeconds);
            clip.durationSeconds = (std::min)(1.0f,
                (std::max)(SEQUENCER::kMinSequenceClipDurationSeconds,
                           sequence.durationSeconds - clip.startTimeSeconds));
            sequence.actorAnimationTrack.clips.push_back(std::move(clip));
            Select(SequencePresentationItemKind::ActorAnimation,
                   sequence.actorAnimationTrack.clips.back().id);
            result.sequenceChanged = true;
            result.selectionChanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("+ VFX")) {
        SEQUENCER::SequenceVfxCue cue{};
        cue.id = SEQUENCER::AllocateSequenceVfxCueId(sequence.vfxTrack);
        cue.ownerBindingId = selectedBindingId_;
        cue.timeSeconds = SnapTime(playheadSeconds);
        sequence.vfxTrack.cues.push_back(std::move(cue));
        Select(SequencePresentationItemKind::Vfx,
               sequence.vfxTrack.cues.back().id);
        result.sequenceChanged = true;
        result.selectionChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Slow Motion")) {
        SEQUENCER::SequenceTimeDilationClip clip{};
        clip.id = SEQUENCER::AllocateSequenceTimeDilationClipId(
            sequence.timeDilationTrack);
        clip.startTimeSeconds = SnapTime(playheadSeconds);
        clip.durationSeconds = (std::min)(0.35f,
            (std::max)(SEQUENCER::kMinSequenceClipDurationSeconds,
                       sequence.durationSeconds - clip.startTimeSeconds));
        sequence.timeDilationTrack.clips.push_back(clip);
        Select(SequencePresentationItemKind::TimeDilation, clip.id);
        result.sequenceChanged = true;
        result.selectionChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Event")) {
        SEQUENCER::SequenceSignalCue cue{};
        cue.id = SEQUENCER::AllocateSequenceSignalCueId(sequence.signalTrack);
        cue.targetBindingId = selectedBindingId_;
        cue.name = "Sequence.Event";
        cue.timeSeconds = SnapTime(playheadSeconds);
        sequence.signalTrack.cues.push_back(std::move(cue));
        Select(SequencePresentationItemKind::Signal,
               sequence.signalTrack.cues.back().id);
        result.sequenceChanged = true;
        result.selectionChanged = true;
    }
    if (!editingAllowed) ImGui::EndDisabled();

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize{
        (std::max)(ImGui::GetContentRegionAvail().x, 540.0f),
        kRulerHeight + kTrackHeight * 4.0f + 4.0f};
    ImGui::InvisibleButton(
        "##StageTimelineCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    const ImVec2 canvasMax{canvasMin.x + canvasSize.x,
                           canvasMin.y + canvasSize.y};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(12, 15, 20, 255), 5.0f);
    draw->AddRect(canvasMin, canvasMax, IM_COL32(57, 64, 76, 255), 5.0f);
    const float originX = canvasMin.x + kTrackLabelWidth;
    draw->AddLine(ImVec2(originX, canvasMin.y), ImVec2(originX, canvasMax.y),
                  IM_COL32(78, 87, 103, 255));

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        panning_ = true;
        panStartMouseX_ = ImGui::GetMousePos().x;
        panStartScrollTime_ = scrollTimeSeconds_;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
    if (panning_) {
        scrollTimeSeconds_ = (std::max)(
            0.0f, panStartScrollTime_ -
                      (ImGui::GetMousePos().x - panStartMouseX_) /
                          pixelsPerSecond_);
    }
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float mouseTime = scrollTimeSeconds_ +
            (ImGui::GetMousePos().x - originX) / pixelsPerSecond_;
        const float oldPixels = pixelsPerSecond_;
        pixelsPerSecond_ = std::clamp(
            pixelsPerSecond_ * std::pow(1.14f, ImGui::GetIO().MouseWheel),
            36.0f, 420.0f);
        if (ImGui::GetMousePos().x >= originX) {
            scrollTimeSeconds_ = (std::max)(
                0.0f, mouseTime -
                          (ImGui::GetMousePos().x - originX) /
                              pixelsPerSecond_);
        }
        (void)oldPixels;
    }

    const float visibleStart = scrollTimeSeconds_;
    const float visibleEnd = visibleStart +
        (canvasSize.x - kTrackLabelWidth) / pixelsPerSecond_;
    const float major = pixelsPerSecond_ >= 250.0f ? 0.25f
        : pixelsPerSecond_ >= 120.0f ? 0.5f : 1.0f;
    const float firstTick = std::floor(visibleStart / major) * major;
    for (float time = firstTick; time <= visibleEnd + major; time += major) {
        const float x = originX + (time - visibleStart) * pixelsPerSecond_;
        if (x < originX) continue;
        draw->AddLine(ImVec2(x, canvasMin.y + 15.0f), ImVec2(x, canvasMax.y),
                      IM_COL32(42, 48, 59, 255));
        char label[24]{};
        std::snprintf(label, sizeof(label), "%.2f", time);
        draw->AddText(ImVec2(x + 3.0f, canvasMin.y + 2.0f),
                      IM_COL32(153, 163, 179, 255), label);
    }

    const std::array kinds{
        SequencePresentationItemKind::ActorAnimation,
        SequencePresentationItemKind::Vfx,
        SequencePresentationItemKind::TimeDilation,
        SequencePresentationItemKind::Signal};
    const std::array names{ "Actor Animation", "VFX", "Time Dilation", "Signal" };
    for (int row = 0; row < static_cast<int>(kinds.size()); ++row) {
        const float y = canvasMin.y + kRulerHeight + row * kTrackHeight;
        if ((row & 1) != 0) {
            draw->AddRectFilled(ImVec2(canvasMin.x + 1.0f, y),
                                ImVec2(canvasMax.x - 1.0f, y + kTrackHeight),
                                IM_COL32(17, 21, 28, 255));
        }
        draw->AddText(ImVec2(canvasMin.x + 10.0f, y + 8.0f),
                      IM_COL32(198, 206, 218, 255), names[row]);
        draw->AddLine(ImVec2(canvasMin.x, y + kTrackHeight),
                      ImVec2(canvasMax.x, y + kTrackHeight),
                      IM_COL32(34, 40, 49, 255));
    }

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= originX && mouse.y < canvasMin.y + kRulerHeight) {
            result.requestedPlayheadSeconds = std::clamp(
                SnapTime(visibleStart + (mouse.x - originX) /
                                           pixelsPerSecond_),
                0.0f, sequence.durationSeconds);
            scrubbing_ = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) scrubbing_ = false;
    if (scrubbing_) {
        result.requestedPlayheadSeconds = std::clamp(
            SnapTime(visibleStart +
                     (ImGui::GetMousePos().x - originX) / pixelsPerSecond_),
            0.0f, sequence.durationSeconds);
    }

    const auto rowY = [canvasMin](int row) {
        return canvasMin.y + kRulerHeight + row * kTrackHeight;
    };
    const auto drawRange = [&](SequencePresentationItemKind kind, int row,
                               uint64_t id, std::string_view label,
                               float& start, float& duration) {
        const float x0 = originX + (start - visibleStart) * pixelsPerSecond_;
        const float x1 = originX +
            (start + duration - visibleStart) * pixelsPerSecond_;
        if (x1 < originX || x0 > canvasMax.x) return;
        const ImVec2 minimum{(std::max)(originX, x0), rowY(row) + 4.0f};
        const ImVec2 maximum{(std::min)(canvasMax.x, x1),
                             rowY(row) + kTrackHeight - 4.0f};
        const bool selected = selectedKind_ == kind && selectedItemId_ == id;
        draw->AddRectFilled(minimum, maximum, TrackColor(kind, selected), 4.0f);
        if (selected) {
            draw->AddRect(minimum, maximum, IM_COL32(245, 248, 255, 245),
                          4.0f, 0, 2.0f);
        }
        if (maximum.x - minimum.x > 34.0f) {
            draw->PushClipRect(minimum, maximum, true);
            draw->AddText(ImVec2(minimum.x + 7.0f, minimum.y + 5.0f),
                          IM_COL32(10, 14, 20, 255),
                          std::string(label).c_str());
            draw->PopClipRect();
        }
        ImGui::SetCursorScreenPos(minimum);
        ImGui::PushID(static_cast<int>(kind));
        ImGui::PushID(static_cast<int>(id));
        ImGui::InvisibleButton("##StageRange", ImVec2(
            (std::max)(5.0f, maximum.x - minimum.x),
            maximum.y - minimum.y));
        if (ImGui::IsItemClicked()) {
            Select(kind, id);
            result.selectionChanged = true;
        }
        if (editingAllowed && ImGui::IsItemActivated()) {
            dragStartMouseX_ = ImGui::GetMousePos().x;
            dragStartTime_ = start;
            dragStartDuration_ = duration;
            const float localX = ImGui::GetMousePos().x - x0;
            dragMode_ = localX <= 7.0f ? DragMode::ResizeStart
                : (x1 - ImGui::GetMousePos().x <= 7.0f
                       ? DragMode::ResizeEnd
                       : DragMode::Move);
        }
        if (editingAllowed && ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float delta =
                (ImGui::GetMousePos().x - dragStartMouseX_) /
                pixelsPerSecond_;
            const float minDuration = SEQUENCER::kMinSequenceClipDurationSeconds;
            if (dragMode_ == DragMode::Move) {
                start = std::clamp(SnapTime(dragStartTime_ + delta), 0.0f,
                    (std::max)(0.0f, sequence.durationSeconds - duration));
            } else if (dragMode_ == DragMode::ResizeStart) {
                const float end = dragStartTime_ + dragStartDuration_;
                start = std::clamp(SnapTime(dragStartTime_ + delta), 0.0f,
                                   end - minDuration);
                duration = end - start;
            } else if (dragMode_ == DragMode::ResizeEnd) {
                duration = std::clamp(
                    SnapTime(dragStartDuration_ + delta), minDuration,
                    sequence.durationSeconds - start);
            }
            result.sequenceChanged = true;
            result.editMergeId = 0x53510000ull ^ id;
        }
        if (ImGui::IsItemDeactivated()) dragMode_ = DragMode::None;
        ImGui::PopID();
        ImGui::PopID();
    };

    for (auto& clip : sequence.actorAnimationTrack.clips) {
        drawRange(SequencePresentationItemKind::ActorAnimation, 0, clip.id,
                  clip.clipName, clip.startTimeSeconds, clip.durationSeconds);
    }
    for (auto& clip : sequence.timeDilationTrack.clips) {
        drawRange(SequencePresentationItemKind::TimeDilation, 2, clip.id,
                  "Time Scale", clip.startTimeSeconds, clip.durationSeconds);
    }

    const auto drawMarker = [&](SequencePresentationItemKind kind, int row,
                                uint64_t id, float& time, const char* tooltip) {
        const float x = originX + (time - visibleStart) * pixelsPerSecond_;
        if (x < originX || x > canvasMax.x) return;
        const float y = rowY(row) + kTrackHeight * 0.5f;
        const bool selected = selectedKind_ == kind && selectedItemId_ == id;
        const float radius = selected ? 8.0f : 6.0f;
        draw->AddCircleFilled(ImVec2(x, y), radius, TrackColor(kind, selected));
        if (selected) {
            draw->AddCircle(ImVec2(x, y), radius + 2.0f,
                            IM_COL32(245, 248, 255, 240), 0, 2.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(x - 10.0f, y - 10.0f));
        ImGui::PushID(static_cast<int>(kind));
        ImGui::PushID(static_cast<int>(id));
        ImGui::InvisibleButton("##StageMarker", ImVec2(20.0f, 20.0f));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        if (ImGui::IsItemClicked()) {
            Select(kind, id);
            result.selectionChanged = true;
        }
        if (editingAllowed && ImGui::IsItemActivated()) {
            dragStartMouseX_ = ImGui::GetMousePos().x;
            dragStartTime_ = time;
        }
        if (editingAllowed && ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            time = std::clamp(
                SnapTime(dragStartTime_ +
                    (ImGui::GetMousePos().x - dragStartMouseX_) /
                        pixelsPerSecond_),
                0.0f, sequence.durationSeconds);
            result.sequenceChanged = true;
            result.editMergeId = 0x53520000ull ^ id;
        }
        ImGui::PopID();
        ImGui::PopID();
    };
    for (auto& cue : sequence.vfxTrack.cues) {
        drawMarker(SequencePresentationItemKind::Vfx, 1, cue.id,
                   cue.timeSeconds, "VFX cue");
    }
    for (auto& cue : sequence.signalTrack.cues) {
        drawMarker(SequencePresentationItemKind::Signal, 3, cue.id,
                   cue.timeSeconds, cue.name.c_str());
    }

    const float playheadX = originX +
        (playheadSeconds - visibleStart) * pixelsPerSecond_;
    if (playheadX >= originX && playheadX <= canvasMax.x) {
        draw->AddLine(ImVec2(playheadX, canvasMin.y),
                      ImVec2(playheadX, canvasMax.y),
                      playing ? IM_COL32(255, 111, 72, 255)
                              : IM_COL32(235, 90, 62, 255),
                      2.0f);
        draw->AddTriangleFilled(
            ImVec2(playheadX - 5.0f, canvasMin.y),
            ImVec2(playheadX + 5.0f, canvasMin.y),
            ImVec2(playheadX, canvasMin.y + 8.0f),
            IM_COL32(255, 111, 72, 255));
    }

    if (editingAllowed && canvasHovered &&
        ImGui::IsKeyPressed(ImGuiKey_Delete) && DeleteSelection(sequence)) {
        result.sequenceChanged = true;
        result.selectionChanged = true;
    }
    if (result.sequenceChanged) {
        NormalizeCinematicSequence(sequence);
        NormalizeSelection(sequence);
    }
    ImGui::PopID();
#else
    (void)scene;
    (void)sequence;
    (void)sequenceAssetGuid;
    (void)previewBindings;
    (void)selectedObjectId;
    (void)playheadSeconds;
    (void)playing;
    (void)editingAllowed;
    (void)portableAsset;
#endif
    return result;
}

} // namespace HIKARI::EDITOR
