#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawTransport(CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    const bool playing = cursor_.IsPlaying();
    if (IconButton(playing ? EditorGlyph::Pause : EditorGlyph::Play, "sequencePlay", EditorButtonTone::Primary,
                   {28, 28}, TEXT::Localize(playing ? "cine.pause" : "cine.play").c_str())) {
        if (playing)
            cursor_.Pause();
        else {
            if (cursor_.GetTimeSeconds() >= sequence.durationSeconds)
                cursor_.Seek(0);
            SEQUENCER::SequencePlaybackOptions options;
            options.loop = loop_;
            options.holdAtEnd = true;
            cursor_.Play(options);
            previewEnabled_ = true;
        }
    }
    NextToolbarItem();
    if (IconButton(EditorGlyph::Stop, "sequenceStop", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.stopRestore").c_str())) {
        cursor_.Stop();
        previewEnabled_ = false;
    }
    NextToolbarItem();
    if (IconButton(EditorGlyph::StepBackward, "sequencePrevious", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.previousFrame").c_str())) {
        cursor_.Pause();
        cursor_.Seek(cursor_.GetTimeSeconds() - 1.0f / 60);
        previewEnabled_ = true;
    }
    NextToolbarItem();
    if (IconButton(EditorGlyph::StepForward, "sequenceNext", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.nextFrame").c_str())) {
        cursor_.Pause();
        cursor_.Seek(cursor_.GetTimeSeconds() + 1.0f / 60);
        previewEnabled_ = true;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Loop, "sequenceLoop", loop_, {28, 28}, TEXT::Localize("cine.loop").c_str())) {
        loop_ = !loop_;
        if (cursor_.IsPlaying()) {
            SEQUENCER::SequencePlaybackOptions options;
            options.loop = loop_;
            options.holdAtEnd = true;
            cursor_.Play(options);
        }
    }
    NextToolbarItem(110);
    float seconds = cursor_.GetTimeSeconds();
    ImGui::SetNextItemWidth(95);
    if (ImGui::DragFloat("##time", &seconds, 1.0f / 60, 0, sequence.durationSeconds, "%.3f s")) {
        cursor_.Pause();
        cursor_.Seek(seconds);
        previewEnabled_ = true;
    }
    NextToolbarItem(110);
    ImGui::TextDisabled(TEXT::Localize("editor.ui.2f.s.c5fe30").c_str(), sequence.durationSeconds);
    NextToolbarItem(110);
    ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat(TEXT::LocalizeWindow("ui.input.scale", "缩放").c_str(), &zoom_, 20, 240, "%.0f");
    NextToolbarItem();
    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.screenFx").c_str(), "addSequenceEffect")) {
        SEQUENCER::SequenceScreenEffect e;
        for (const auto& effect : sequence.screenEffects)
            e.id = (std::max)(e.id, effect.id + 1);
        e.start = cursor_.GetTimeSeconds();
        sequence.screenEffects.push_back(e);
        selectionKind_ = SelectionKind::Effect;
        selectedId_ = e.id;
        changed = true;
    }
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::List, "keyframeMode", !curveMode_, {28, 28}, TEXT::Localize("cine.keys").c_str()))
        curveMode_ = false;
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Animation, "curveMode", curveMode_, {28, 28},
                         TEXT::Localize("cine.editCurve").c_str()))
        curveMode_ = true;
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Filter, "animatedChannels", animatedChannelsOnly_, {28, 28},
                         TEXT::Localize("cine.animatedChannels").c_str()))
        animatedChannelsOnly_ = !animatedChannelsOnly_;
    NextToolbarItem();
    if (IconButton(EditorGlyph::Focus, "fitTimeline", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.fitTimeline").c_str()))
        zoom_ = std::clamp((ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 200) /
                               (std::max)(1.0f, sequence.durationSeconds),
                           10.0f, 400.0f);
    ImGui::Separator();
#endif
    return changed;
}

// 镜头の入り方。前の镜头と接している時だけ（EvaluateSequenceCameraView と同じ判定）、混ぜる区間を帯と SmoothStep の
// 線で、硬切りを縦の線で示す。混ぜるのは前の镜头の最後の画を止めたまま、この镜头の始めから durationSeconds の間。
void CinematicsWorkspaceController::DrawShotTransition(ImDrawList& draw, const CinematicSequence& sequence,
                                                       const SEQUENCER::CameraCutClip& shot, float timeOriginX, float y,
                                                       float rowHeight) const {
    using namespace SEQUENCER;
    const float before = std::nextafter(shot.startTimeSeconds, -1.0f);
    const bool joined = std::any_of(sequence.cameraCutTrack.clips.begin(), sequence.cameraCutTrack.clips.end(),
                                    [&](const CameraCutClip& other) {
                                        return other.id != shot.id && other.startTimeSeconds <= before &&
                                               before < other.startTimeSeconds + other.durationSeconds;
                                    });
    if (!joined)
        return;
    const float x = timeOriginX + shot.startTimeSeconds * zoom_;
    const float top = y + 3, bottom = y + rowHeight - 3;
    if (shot.transition.mode != CameraCutTransitionMode::EaseInOut || shot.transition.durationSeconds <= 0) {
        draw.AddLine({x, top - 2}, {x, bottom + 2}, IM_COL32(235, 245, 255, 230), 2);
        return;
    }
    const float width = shot.transition.durationSeconds * zoom_;
    draw.AddRectFilled({x, top}, {x + width, bottom}, IM_COL32(235, 245, 255, 45), 4);
    ImVec2 previous{x, bottom};
    for (int step = 1; step <= 12; ++step) {
        const float t = step / 12.0f;
        const ImVec2 point{x + width * t, bottom - (bottom - top) * t * t * (3 - 2 * t)};
        draw.AddLine(previous, point, IM_COL32(235, 245, 255, 220), 1.5f);
        previous = point;
    }
}

void CinematicsWorkspaceController::DrawShotHoverPreview(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                         const SEQUENCER::CameraCutClip* shot, float localTime) {
    namespace VIEW = RENDER3D::EDITORVIEW;
    if (shot == nullptr) {
        if (shotPreviewSubmitted_)
            VIEW::ClearRequest(kCinematicsShotPreviewRenderViewId);
        shotPreviewSubmitted_ = false;
        return;
    }
    constexpr float kWidth = 256.0f, kHeight = 144.0f;
    // 1/60 秒単位の時刻と镜头で描き直しを決める（同じ所に置いている間は描き直さない）。
    const uint64_t frame = static_cast<uint64_t>(std::lround(localTime * 60.0f));
    VIEW::EditorInteractiveViewRequest request;
    request.viewId = kCinematicsShotPreviewRenderViewId;
    request.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    request.width = static_cast<uint32_t>(kWidth);
    request.height = static_cast<uint32_t>(kHeight);
    request.cameraFrame.camera = ResolveShotCamera(scene, sequence, *shot, localTime);
    request.cameraFrame.valid = true;
    request.cameraFrame.cameraCut = true;
    request.cameraFrame.revision = (shot->id << 20) ^ frame;
    request.sceneRevision = scene.GetSceneDocumentRevision();
    request.visible = true;
    request.drawDebug = false;
    VIEW::SubmitRequest(request);
    shotPreviewSubmitted_ = true;
    ImGui::BeginTooltip();
    ImGui::Text(TEXT::Localize("cine.clipTip").c_str(), shot->camera.name.c_str(), shot->startTimeSeconds,
                shot->startTimeSeconds + shot->durationSeconds);
    const VIEW::EditorInteractiveViewOutput output = VIEW::GetOutput(kCinematicsShotPreviewRenderViewId);
    if (output.ready && output.colorSrv.ptr != 0)
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(output.colorSrv.ptr)), {kWidth, kHeight});
    else
        ImGui::Dummy({kWidth, kHeight});
    ImGui::TextDisabled("%.2f s", shot->startTimeSeconds + localTime);
    ImGui::EndTooltip();
}

bool CinematicsWorkspaceController::AddPoseKeyAtCursor(CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr || !shot->camera.owned)
        return false;
    const float local = std::clamp(cursor_.GetTimeSeconds() - shot->startTimeSeconds, 0.0f, shot->durationSeconds);
    SEQUENCER::SetShotPose(shot->camera, SEQUENCER::EvaluateShotPose(shot->camera, local), true);
    SEQUENCER::NormalizeShotCamera(shot->camera);
    previewChanged_ = true;
    previewEnabled_ = true;
    return true;
}

bool CinematicsWorkspaceController::DrawPoseKeyLane(DocumentSceneBase&, CinematicSequence& sequence, float x, float y,
                                                    float width, float labelWidth, float rowHeight) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr || !shot->camera.owned)
        return false;
    auto* draw = ImGui::GetWindowDrawList();
    const float middle = y + rowHeight * 0.5f;
    const float timeX = x + labelWidth + shot->startTimeSeconds * zoom_;
    const float cursorLocal = cursor_.GetTimeSeconds() - shot->startTimeSeconds;
    const auto times = SEQUENCER::CollectShotPoseKeyTimes(shot->camera);
    std::optional<float> remove;
    ImGui::PushID("poseKeys");
    // 菱形の押し判定を背景より先に置く（先に置いた項目が重なりの上で優先される）。
    for (size_t i = 0; i < times.size(); ++i) {
        const float time = times[i];
        const float px = timeX + time * zoom_;
        const bool current = std::abs(time - cursorLocal) <= 0.5f / 60.0f;
        const float r = 6.5f;
        const ImVec2 diamond[]{{px, middle - r}, {px + r, middle}, {px, middle + r}, {px - r, middle}};
        draw->AddConvexPolyFilled(diamond, 4, current ? IM_COL32(255, 207, 108, 255) : IM_COL32(123, 211, 221, 255));
        draw->AddPolyline(diamond, 4, IM_COL32(10, 14, 20, 255), ImDrawFlags_Closed, 1.5f);
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetCursorScreenPos({px - 8, middle - 9});
        ImGui::InvisibleButton("poseKey", {16, 18});
        if (ImGui::IsItemActivated()) {
            poseDragTime_ = time;
            cursor_.Pause();
            cursor_.Seek(shot->startTimeSeconds + time);
            previewEnabled_ = true;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && poseDragTime_ >= 0.0f) {
            // 隣の键を越えない範囲で、全ての姿勢の曲線の键を一緒に動かす（1/60 秒に吸着、Alt で自由）。
            float next = (ImGui::GetIO().MousePos.x - timeX) / zoom_;
            if (!ImGui::GetIO().KeyAlt)
                next = std::round(next * 60) / 60;
            const float lo = i ? times[i - 1] + 1.0f / 120 : 0.0f;
            const float hi = i + 1 < times.size() ? times[i + 1] - 1.0f / 120 : shot->durationSeconds;
            next = std::clamp(next, lo, (std::max)(lo, hi));
            if (std::abs(next - poseDragTime_) > 0.00001f &&
                SEQUENCER::MoveShotPoseKeys(shot->camera, poseDragTime_, next)) {
                poseDragTime_ = next;
                cursor_.Seek(shot->startTimeSeconds + next);
                previewChanged_ = true;
                changed = true;
            }
        }
        if (ImGui::IsItemDeactivated())
            poseDragTime_ = -1.0f;
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(0))
            ImGui::SetTooltip(TEXT::Localize("cine.keyTip").c_str(), shot->startTimeSeconds + time);
        if (ImGui::BeginPopupContextItem("poseKeyMenu")) {
            if (ImGui::MenuItem(TEXT::Localize("cine.deleteKey").c_str()))
                remove = time;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    // 帯の空いた所を二度押すと、その時刻の今の姿勢を键にする。
    ImGui::SetCursorScreenPos({x + labelWidth, y});
    ImGui::InvisibleButton("poseLane", {(std::max)(1.0f, width - labelWidth), rowHeight});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        float time = std::clamp((ImGui::GetIO().MousePos.x - timeX) / zoom_, 0.0f, shot->durationSeconds);
        if (!ImGui::GetIO().KeyAlt)
            time = std::round(time * 60) / 60;
        cursor_.Seek(shot->startTimeSeconds + time);
        changed |= AddPoseKeyAtCursor(sequence);
    } else if (ImGui::IsItemHovered() && times.empty())
        ImGui::SetTooltip("%s", TEXT::Localize("cine.poseLaneHint").c_str());
    if (remove) {
        changed |= SEQUENCER::RemoveShotPoseKeys(shot->camera, *remove);
        previewChanged_ = true;
    }
    ImGui::PopID();
#endif
    return changed;
}

bool CinematicsWorkspaceController::DrawTimeline(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    if (curveMode_)
        return DrawChannelCurve(sequence);
    constexpr float labelWidth = 175, rowHeight = 30, ruler = 25;
    const auto channelCount = CollectChannels(sequence, true).size();
    const auto* poseShot = SelectedShot(sequence);
    const bool poseLane = poseShot != nullptr && poseShot->camera.owned;
    if (!ImGui::BeginChild("SequenceCanvas", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return false;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const float width =
        (std::max)(ImGui::GetContentRegionAvail().x, labelWidth + (sequence.durationSeconds + 1) * zoom_);
    const float height =
        (2 + (poseLane ? 1 : 0) + channelCount + sequence.presentationCueTrack.clips.size() +
         sequence.screenEffects.size()) *
            rowHeight +
        ruler;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(15, 20, 27, 255), 5);
    const float step = zoom_ >= 60 ? 1.0f : 5.0f;
    for (float t = 0; t <= sequence.durationSeconds + 1; t += step) {
        const float x = origin.x + labelWidth + t * zoom_;
        draw->AddLine({x, origin.y}, {x, origin.y + height}, IM_COL32(38, 46, 56, 255));
        char text[24];
        std::snprintf(text, sizeof(text), "%.0f", t);
        draw->AddText({x + 4, origin.y + 3}, IM_COL32(130, 145, 164, 255), text);
    }
    ImGui::SetCursorScreenPos({origin.x + labelWidth, origin.y});
    ImGui::InvisibleButton("ruler", {width - labelWidth, ruler});
    if (ImGui::IsItemActive()) {
        cursor_.Pause();
        cursor_.Seek((ImGui::GetIO().MousePos.x - origin.x - labelWidth) / zoom_);
        previewEnabled_ = true;
    }
    int row = 0;
    const SEQUENCER::CameraCutClip* hoveredShot = nullptr;
    float hoveredLocalTime = 0.0f;
    const auto lane = [&](const char* name) {
        const float y = origin.y + ruler + row++ * rowHeight;
        draw->AddText({origin.x + 8, y + 7}, IM_COL32(155, 172, 192, 255), name);
        draw->AddLine({origin.x, y + rowHeight}, {origin.x + width, y + rowHeight}, IM_COL32(30, 38, 47, 255));
        return y;
    };
    const auto clip = [&](uint64_t id, SelectionKind kind, float& start, float& duration, const char* name, float y,
                          ImU32 color) {
        const float x = origin.x + labelWidth + start * zoom_, w = (std::max)(10.0f, duration * zoom_);
        ImGui::PushID(static_cast<int>(kind));
        ImGui::PushID(static_cast<int>(id));
        draw->AddRectFilled({x, y + 3}, {x + w, y + rowHeight - 3}, color, 4);
        if (kind == selectionKind_ && id == selectedId_)
            draw->AddRect({x, y + 3}, {x + w, y + rowHeight - 3}, IM_COL32(235, 245, 255, 255), 4, 0, 2);
        draw->PushClipRect({x + 7, y + 4}, {x + w - 7, y + rowHeight - 4}, true);
        draw->AddText({x + 8, y + 8}, IM_COL32(240, 244, 248, 255), name);
        draw->PopClipRect();
        ImGui::SetCursorScreenPos({x, y + 3});
        ImGui::InvisibleButton("clip", {w, rowHeight - 6});
        if (ImGui::IsItemActivated()) {
            selectedId_ = id;
            selectionKind_ = kind;
            curveView_.selectedKey = -1;
            draggingId_ = id;
            draggingKind_ = kind;
            dragStart_ = start;
            dragDuration_ = duration;
            dragMouseX_ = ImGui::GetIO().MousePos.x;
            dragPart_ = dragMouseX_ < x + 6 ? -1 : dragMouseX_ > x + w - 6 ? 1 : 0;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            const float delta = (ImGui::GetIO().MousePos.x - dragMouseX_) / zoom_;
            const auto snap = [](float t) { return std::round(t * 60) / 60; };
            const float next = ImGui::GetIO().KeyAlt ? delta : snap(delta);
            if (dragPart_ == 1)
                duration = (std::max)(0.1f, dragDuration_ + next);
            else if (dragPart_ == -1) {
                start = std::clamp(dragStart_ + next, 0.0f, dragStart_ + dragDuration_ - 0.1f);
                duration = dragDuration_ + dragStart_ - start;
            } else
                start = (std::max)(0.0f, dragStart_ + next);
            changed = true;
        }
        // 镜头は置いた時刻の画を DrawShotHoverPreview で添える。
        if (ImGui::IsItemHovered() && kind != SelectionKind::Shot)
            ImGui::SetTooltip(TEXT::Localize("cine.clipTip").c_str(), name, start, start + duration);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            cursor_.Seek(start);
            previewEnabled_ = true;
        }
        ImGui::PopID();
        ImGui::PopID();
    };
    const float shotY = lane(TEXT::Localize("cine.shot").c_str());
    for (auto& s : sequence.cameraCutTrack.clips) {
        clip(s.id, SelectionKind::Shot, s.startTimeSeconds, s.durationSeconds, s.camera.name.c_str(), shotY,
             IM_COL32(44, 105, 151, 255));
        DrawShotTransition(*draw, sequence, s, origin.x + labelWidth, shotY, rowHeight);
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            hoveredShot = &s;
            hoveredLocalTime =
                std::clamp((ImGui::GetIO().MousePos.x - origin.x - labelWidth) / zoom_ - s.startTimeSeconds, 0.0f,
                           s.durationSeconds);
        }
        for (const auto& channel : s.camera.channels)
            for (const auto& key : channel.keys) {
                if (key.time > s.durationSeconds)
                    continue;
                const float px = origin.x + labelWidth + (s.startTimeSeconds + key.time) * zoom_;
                draw->AddLine({px, shotY + rowHeight - 7}, {px, shotY + rowHeight - 3}, IM_COL32(173, 231, 247, 200));
            }
    }
    DrawShotHoverPreview(scene, sequence, hoveredShot, hoveredLocalTime);
    if (poseLane) {
        const float poseY = lane(TEXT::Localize("cine.poseKeys").c_str());
        changed |= DrawPoseKeyLane(scene, sequence, origin.x, poseY, width, labelWidth, rowHeight);
    }
    changed |= DrawChannelLanes(sequence, origin.x, origin.y + ruler + row * rowHeight, width, labelWidth, rowHeight);
    row += static_cast<int>(channelCount);
    for (auto& cue : sequence.presentationCueTrack.clips) {
        const auto y = lane(
            TEXT::Localize(cue.kind == PresentationCueKind::Vfx ? "ui.assetBrowser.typeVfx" : "cine.sound").c_str());
        clip(cue.id, SelectionKind::Cue, cue.startSeconds, cue.durationSeconds, cue.name.c_str(), y,
             cue.kind == PresentationCueKind::Vfx ? IM_COL32(143, 78, 48, 255) : IM_COL32(51, 116, 89, 255));
    }
    for (auto& effect : sequence.screenEffects) {
        const auto y = lane(TEXT::Localize("cine.screenFx").c_str());
        const auto name = effect.name.empty() ? "画面效果" : effect.name;
        clip(effect.id, SelectionKind::Effect, effect.start, effect.duration, name.c_str(), y,
             IM_COL32(105, 71, 146, 255));
    }
    const float dropY = lane("添加素材");
    ImGui::SetCursorScreenPos({origin.x + labelWidth, dropY});
    ImGui::InvisibleButton("dropCue", {width - labelWidth, rowHeight});
    if (ImGui::BeginDragDropTarget()) {
        const auto accept = [&](const ImGuiPayload* payload, PresentationCueKind kind) {
            if (!payload || !payload->Data || !payload->DataSize)
                return;
            PresentationCueClip cue;
            for (const auto& c : sequence.presentationCueTrack.clips)
                cue.id = (std::max)(cue.id, c.id + 1);
            cue.kind = kind;
            cue.name = TEXT::Localize(kind == PresentationCueKind::Vfx ? "ui.assetBrowser.typeVfx" : "cine.sound");
            cue.durationSeconds = 3;
            cue.startSeconds = (std::max)(0.0f, (ImGui::GetIO().MousePos.x - origin.x - labelWidth) / zoom_);
            const char* bytes = static_cast<const char*>(payload->Data);
            if (bytes[payload->DataSize - 1] != '\0')
                return;
            const std::string data(bytes, static_cast<size_t>(payload->DataSize - 1));
            if (kind == PresentationCueKind::Vfx) {
                const auto* record = scene.GetAssetDatabase().FindByGuid(AssetGuid{data});
                if (!record || record->type != AssetType::VfxEffect) {
                    pendingStatusMessage_ = TEXT::Localize("cine.vfxRequired");
                    return;
                }
                cue.vfxAsset.value = data;
                cue.name = GetAssetRecordDisplayName(*record);
            } else
                cue.soundPath = data;
            sequence.presentationCueTrack.clips.push_back(cue);
            changed = true;
            selectionKind_ = SelectionKind::Cue;
            selectedId_ = cue.id;
        };
        accept(ImGui::AcceptDragDropPayload(kAssetGuidPayload), PresentationCueKind::Vfx);
        accept(ImGui::AcceptDragDropPayload("HIKARI_SEQUENCE_AUDIO"), PresentationCueKind::Audio);
        ImGui::EndDragDropTarget();
    }
    const float x = origin.x + labelWidth + cursor_.GetTimeSeconds() * zoom_;
    draw->AddLine({x, origin.y}, {x, origin.y + height}, IM_COL32(105, 220, 230, 255), 2);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, height});
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                              ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
        changed |= DeleteSelectedClip(sequence);
    ImGui::EndChild();
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
