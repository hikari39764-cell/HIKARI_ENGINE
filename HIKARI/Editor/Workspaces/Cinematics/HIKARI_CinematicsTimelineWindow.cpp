#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

bool CinematicsWorkspaceController::DrawTimeline(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    if (curveMode_)
        return DrawChannelCurve(sequence);
    constexpr float labelWidth = 175, rowHeight = 30, ruler = 25;
    const auto channelCount = CollectChannels(sequence, true).size();
    if (!ImGui::BeginChild("SequenceCanvas", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return false;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const float width =
        (std::max)(ImGui::GetContentRegionAvail().x, labelWidth + (sequence.durationSeconds + 1) * zoom_);
    const float height =
        (2 + channelCount + sequence.presentationCueTrack.clips.size() + sequence.screenEffects.size()) * rowHeight +
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
        if (ImGui::IsItemHovered())
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
        for (const auto& channel : s.camera.channels)
            for (const auto& key : channel.keys) {
                if (key.time > s.durationSeconds)
                    continue;
                const float px = origin.x + labelWidth + (s.startTimeSeconds + key.time) * zoom_;
                draw->AddLine({px, shotY + rowHeight - 7}, {px, shotY + rowHeight - 3}, IM_COL32(173, 231, 247, 200));
            }
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
