#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Sequencer/Drivers/HIKARI_PresentationCueDriver.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
constexpr float kLabelWidth = 150.0f, kRowHeight = 28.0f, kRuler = 26.0f;

// 目盛りの間隔：主目盛りが 80 画素以上離れる最小の刻み（秒）。
float RulerStep(float zoom) {
    constexpr std::array<float, 11> steps{1.0f / 30, 0.1f, 0.25f, 0.5f, 1, 2, 5, 10, 30, 60, 120};
    for (const float step : steps)
        if (step * zoom >= 80.0f)
            return step;
    return steps.back();
}

void FormatRulerTime(char* text, size_t size, float seconds, float step) {
    if (step < 1.0f)
        std::snprintf(text, size, step < 0.1f ? "%.2f" : "%.1f", seconds);
    else
        std::snprintf(text, size, "%.0fs", seconds);
}

// 再生位置の表示（秒と編集時に選んだ fps のフレーム番号）。
void FormatTimecode(char* text, size_t size, float seconds, float fps) {
    const int rate = static_cast<int>(std::lround(fps));
    const int frames = static_cast<int>(std::lround((std::max)(0.0f, seconds) * fps));
    std::snprintf(text, size, "%d:%02d.%02d", frames / (rate * 60), (frames / rate) % 60, frames % rate);
}
#endif
} // namespace

bool CinematicsWorkspaceController::DrawTransport(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    const auto seek = [&](float seconds) {
        cursor_.Pause();
        cursor_.Seek(std::clamp(seconds, 0.0f, sequence.durationSeconds));
        previewEnabled_ = true;
    };
    const bool playing = cursor_.IsPlaying();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool playKey = ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Space, focused);
    if (IconButton(playing ? EditorGlyph::Pause : EditorGlyph::Play, "sequencePlay", EditorButtonTone::Primary,
                   {28, 28}, TEXT::Localize(playing ? "cine.pause" : "cine.play").c_str()) ||
        playKey) {
        if (playing)
            cursor_.Pause();
        else {
            if (selectionPreviewRange_ && (cursor_.GetTimeSeconds() < selectionPreviewRange_->first ||
                                           cursor_.GetTimeSeconds() >= selectionPreviewRange_->second))
                cursor_.Seek(previewRate_ > 0 ? selectionPreviewRange_->first : selectionPreviewRange_->second);
            if (previewRate_ > 0 && cursor_.GetTimeSeconds() >= sequence.durationSeconds)
                cursor_.Seek(0);
            if (previewRate_ < 0 && cursor_.GetTimeSeconds() <= 0)
                cursor_.Seek(sequence.durationSeconds);
            SEQUENCER::SequencePlaybackOptions options;
            options.loop = loop_ && !selectionPreviewRange_;
            options.playbackRate = previewRate_;
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
        autoPreview_ = false;
    }
    NextToolbarItem();
    if (IconButton(EditorGlyph::StepBackward, "sequencePrevious", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.previousFrame").c_str()))
        seek(cursor_.GetTimeSeconds() - 1.0f / curveView_.fps);
    NextToolbarItem();
    if (IconButton(EditorGlyph::StepForward, "sequenceNext", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.nextFrame").c_str()))
        seek(cursor_.GetTimeSeconds() + 1.0f / curveView_.fps);
    NextToolbarItem();
    if (IconToggleButton(EditorGlyph::Loop, "sequenceLoop", loop_, {28, 28}, TEXT::Localize("cine.loop").c_str())) {
        loop_ = !loop_;
        if (cursor_.IsPlaying()) {
            SEQUENCER::SequencePlaybackOptions options;
            options.loop = loop_ && !selectionPreviewRange_;
            options.playbackRate = previewRate_;
            options.holdAtEnd = true;
            cursor_.Play(options);
        }
    }
    NextToolbarItem(90);
    if (ImGui::Button(selectionPreviewRange_ ? "取消选区循环" : "选区循环")) {
        if (selectionPreviewRange_)
            selectionPreviewRange_.reset();
        else {
            const auto channels = CollectChannels(scene, sequence);
            float first = sequence.durationSeconds, last = 0;
            for (const auto& c : channels)
                for (size_t i = 0; i < c.curve->keys.size(); ++i)
                    if (curveView_.editing.Contains(c.id, i)) {
                        float time = c.offset + c.curve->keys[i].time;
                        if (const auto* shot = SelectedShot(sequence); shot && !shot->camera.timing.keys.empty() &&
                                                                       c.id != "timing" &&
                                                                       !c.id.starts_with("effect/")) {
                            // 選んだ素材時刻を、同じ評価関数の時間写像から再生時刻へ戻す。
                            float low = shot->sourceOffsetSeconds, high = low + shot->durationSeconds;
                            const float source = time - (shot->startTimeSeconds - shot->sourceOffsetSeconds);
                            for (int step = 0; step < 24; ++step) {
                                const float mid = (low + high) * .5f;
                                if (SEQUENCER::ShotMaterialTime(shot->camera, mid) < source)
                                    low = mid;
                                else
                                    high = mid;
                            }
                            time = shot->startTimeSeconds + (low + high) * .5f - shot->sourceOffsetSeconds;
                        }
                        first = (std::min)(first, time);
                        last = (std::max)(last, time);
                    }
            first = std::clamp(first, 0.f, sequence.durationSeconds);
            last = std::clamp(last, 0.f, sequence.durationSeconds);
            if (last > first + .0001f) {
                selectionPreviewRange_ = {first, last};
                seek(first);
            } else
                pendingStatusMessage_ = "请选择至少两个不同时刻的关键帧。";
        }
    }
    NextToolbarItem(90);
    if (ImGui::Button(previewRate_ < 0 ? "倒放预览" : "正放预览")) {
        previewRate_ = -previewRate_;
        cursor_.SetPlaybackRate(previewRate_);
    }
    NextToolbarItem(90);
    ImGui::Text("%.2fx", std::abs(previewRate_));
    NextToolbarItem(90);
    float seconds = cursor_.GetTimeSeconds();
    ImGui::SetNextItemWidth(82);
    if (ImGui::DragFloat("##time", &seconds, 1.0f / curveView_.fps, 0, sequence.durationSeconds, "%.3f s"))
        seek(seconds);
    NextToolbarItem(150);
    // 分:秒.フレーム（編集 fps）で今の位置と全体の長さ。
    char now[32], total[32];
    FormatTimecode(now, sizeof(now), cursor_.GetTimeSeconds(), curveView_.fps);
    FormatTimecode(total, sizeof(total), sequence.durationSeconds, curveView_.fps);
    ImGui::TextDisabled("%s / %s", now, total);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("cine.timecodeTip").c_str());
    NextToolbarItem();
    if (IconButton(EditorGlyph::Focus, "fitTimeline", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("cine.fitTimeline").c_str()))
        fitTimelineRequested_ = true;
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
    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.textTrack").c_str(), "addSequenceText"))
        changed |= AddTextClip(sequence, cursor_.GetTimeSeconds());
    NextToolbarItem();
    changed |= DrawObjectTrackMenu(scene, sequence);
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
    ImGui::TextDisabled("%s", TEXT::Localize("cine.timelineKeysHint").c_str());
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

bool CinematicsWorkspaceController::DrawTimeline(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    if (curveMode_)
        return DrawChannelCurve(scene, sequence);
    changed |= DrawChannelToolbar(scene, sequence);
    const float available = ImGui::GetContentRegionAvail().x;
    if (fitTimelineRequested_ && available > kLabelWidth + 40) {
        const float content =
            (std::max)(1.0f, (std::max)(sequence.durationSeconds, GetCinematicSequenceContentEnd(sequence)));
        zoom_ = std::clamp((available - kLabelWidth - 24) / content, 8.0f, 600.0f);
        fitTimelineRequested_ = false;
    }
    const auto visibleChannels = CollectChannels(scene, sequence, true);
    const auto channelCount =
        std::count_if(visibleChannels.begin(), visibleChannels.end(), [](const auto& c) { return c.visible; });
    if (!ImGui::BeginChild("SequenceCanvas", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return false;
    }
    const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    auto& io = ImGui::GetIO();
    const auto origin = ImGui::GetCursorScreenPos();
    const float timeOriginX = origin.x + kLabelWidth;
    // Ctrl + 滚轮：指の下の時刻を止めたまま拡縮する。中ボタンで左右へ送る。
    if (canvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f) {
        const float time = (io.MousePos.x - timeOriginX) / zoom_;
        const float next = std::clamp(zoom_ * std::pow(1.18f, io.MouseWheel), 8.0f, 2000.0f);
        ImGui::SetScrollX(ImGui::GetScrollX() + time * (next - zoom_));
        zoom_ = next;
    }
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
    const float contentEnd = (std::max)(sequence.durationSeconds, GetCinematicSequenceContentEnd(sequence)) + 1.0f;
    const float width = (std::max)(available, kLabelWidth + contentEnd * zoom_);
    // 事件段は名前ごとに一行（同じ名前の繰り返しは同じ行に並ぶ）。行の順は事件轨の中で名前が最初に出てくる順で、
    // 片段を引いて時刻を変えても行が入れ替わらない。
    std::vector<std::string> eventLanes;
    for (const auto& event : sequence.eventTrack.clips)
        if (std::find(eventLanes.begin(), eventLanes.end(), event.name) == eventLanes.end())
            eventLanes.push_back(event.name);
    const size_t textRowCount = AssignTextRows(sequence);
    const float rows = 4 + static_cast<float>(channelCount + sequence.objectTracks.size() +
                                              sequence.presentationCueTrack.clips.size() +
                                              sequence.screenEffects.size() + eventLanes.size() + textRowCount);
    const float height = rows * kRowHeight + kRuler;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(15, 20, 27, 255), 5);
    // 目盛り（主目盛りに時刻、副目盛りは 5 等分）と演出の終わり。
    const float step = RulerStep(zoom_);
    for (int index = 0; index * step <= contentEnd; ++index) {
        const float t = index * step;
        const float x = timeOriginX + t * zoom_;
        draw->AddLine({x, origin.y}, {x, origin.y + height}, IM_COL32(38, 46, 56, 255));
        char text[24];
        FormatRulerTime(text, sizeof(text), t, step);
        draw->AddText({x + 4, origin.y + 4}, IM_COL32(130, 145, 164, 255), text);
        for (int minor = 1; minor < 5; ++minor) {
            const float mx = x + step * zoom_ * minor / 5.0f;
            draw->AddLine({mx, origin.y + kRuler - 6}, {mx, origin.y + kRuler}, IM_COL32(60, 72, 86, 255));
        }
    }
    const float endX = timeOriginX + sequence.durationSeconds * zoom_;
    draw->AddRectFilled({endX, origin.y + kRuler}, {origin.x + width, origin.y + height}, IM_COL32(0, 0, 0, 70));
    ImGui::SetCursorScreenPos({timeOriginX, origin.y});
    ImGui::InvisibleButton("ruler", {(std::max)(1.0f, width - kLabelWidth), kRuler});
    if (ImGui::IsItemActive()) {
        cursor_.Pause();
        cursor_.Seek(std::clamp(std::round((io.MousePos.x - timeOriginX) / zoom_ * 60.0f) / 60.0f, 0.0f,
                                sequence.durationSeconds));
        previewEnabled_ = true;
    }
    int row = 0;
    std::vector<std::pair<float, std::string>> labels;
    const SEQUENCER::CameraCutClip* hoveredShot = nullptr;
    float hoveredLocalTime = 0.0f;
    const auto lane = [&](std::string name) {
        const float y = origin.y + kRuler + row++ * kRowHeight;
        draw->AddLine({origin.x, y + kRowHeight}, {origin.x + width, y + kRowHeight}, IM_COL32(30, 38, 47, 255));
        labels.emplace_back(y, std::move(name));
        return y;
    };
    // 片段を引く：本体は前後の镜头に重ならない範囲で動かし、端は長さを変える（境目と 1/60 秒に吸着、Alt で自由）。
    const auto clip = [&](uint64_t id, SelectionKind kind, float& start, float& duration, const char* name, float y,
                          ImU32 color) {
        const float x = timeOriginX + start * zoom_, w = (std::max)(10.0f, duration * zoom_);
        ImGui::PushID(static_cast<int>(kind));
        ImGui::PushID(static_cast<int>(id));
        draw->AddRectFilled({x, y + 3}, {x + w, y + kRowHeight - 3}, color, 4);
        if (kind == selectionKind_ && id == selectedId_)
            draw->AddRect({x, y + 3}, {x + w, y + kRowHeight - 3}, IM_COL32(235, 245, 255, 255), 4, 0, 2);
        draw->PushClipRect({x + 7, y + 4}, {x + w - 7, y + kRowHeight - 4}, true);
        draw->AddText({x + 8, y + 7}, IM_COL32(240, 244, 248, 255), name);
        draw->PopClipRect();
        ImGui::SetCursorScreenPos({x, y + 3});
        // 片段の上に後から置く小さな取っ手（音声の淡入・淡出）を掴めるようにする。
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("clip", {w, kRowHeight - 6});
        const bool hovered = ImGui::IsItemHovered();
        // 端を掴むと長さを変える。細い片段（一瞬の事件など）は端の範囲が全体を覆うので、端を作らず全体を動かす。
        const float edge = w >= 24.0f ? 6.0f : 0.0f;
        if (hovered && edge > 0.0f && (io.MousePos.x < x + edge || io.MousePos.x > x + w - edge))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) {
            selectedId_ = id;
            selectionKind_ = kind;
            curveView_.editing.selection.clear();
            draggingId_ = id;
            draggingKind_ = kind;
            dragStart_ = start;
            dragDuration_ = duration;
            dragMouseX_ = io.MousePos.x;
            dragPart_ = edge > 0.0f && dragMouseX_ < x + edge ? -1 : edge > 0.0f && dragMouseX_ > x + w - edge ? 1 : 0;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            const float delta = (io.MousePos.x - dragMouseX_) / zoom_;
            // 镜头どうしは重ねない。引き始めの並びで前後の镜头の端を境にする。
            float lower = 0.0f, upper = 1.0e9f;
            if (kind == SelectionKind::Shot)
                for (const auto& other : sequence.cameraCutTrack.clips) {
                    if (other.id == id)
                        continue;
                    const float otherEnd = other.startTimeSeconds + other.durationSeconds;
                    if (otherEnd <= dragStart_ + 1.0e-4f)
                        lower = (std::max)(lower, otherEnd);
                    else if (other.startTimeSeconds >= dragStart_ + dragDuration_ - 1.0e-4f)
                        upper = (std::min)(upper, other.startTimeSeconds);
                }
            const uint64_t ignore = kind == SelectionKind::Shot ? id : 0;
            if (dragPart_ == 1) {
                const float end = SnapTimelineTime(sequence, dragStart_ + dragDuration_ + delta, ignore);
                duration = std::clamp(end, start + 0.1f, (std::max)(start + 0.1f, upper)) - start;
            } else if (dragPart_ == -1) {
                const float end = dragStart_ + dragDuration_;
                start = std::clamp(SnapTimelineTime(sequence, dragStart_ + delta, ignore), lower, end - 0.1f);
                duration = end - start;
            } else {
                start = std::clamp(SnapTimelineTime(sequence, dragStart_ + delta, ignore), lower,
                                   (std::max)(lower, upper - duration));
            }
            changed = true;
        }
        // 镜头は置いた時刻の画を DrawShotHoverPreview で添える。
        if (hovered && kind != SelectionKind::Shot)
            ImGui::SetTooltip(TEXT::Localize("cine.clipTip").c_str(), name, start, start + duration);
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            cursor_.Seek(start);
            previewEnabled_ = true;
        }
        ImGui::PopID();
        ImGui::PopID();
        return hovered;
    };
    const float shotY = lane(TEXT::Localize("cine.shot"));
    for (auto& s : sequence.cameraCutTrack.clips) {
        char name[160];
        std::snprintf(name, sizeof(name), "%s  %.1fs", s.camera.name.c_str(), s.durationSeconds);
        const float oldStart = s.startTimeSeconds;
        const float oldEnd = oldStart + s.durationSeconds;
        const bool hovered = clip(s.id, SelectionKind::Shot, s.startTimeSeconds, s.durationSeconds, name, shotY,
                                  IM_COL32(44, 105, 151, 255));
        // 左端のトリムは素材開始を移す。片段の移動は素材を変えない。
        if (draggingId_ == s.id && dragPart_ == -1 && s.startTimeSeconds != oldStart) {
            const float start = (std::max)(s.startTimeSeconds, oldStart - s.sourceOffsetSeconds);
            s.sourceOffsetSeconds += start - oldStart;
            s.startTimeSeconds = start;
            s.durationSeconds = oldEnd - start;
        }
        DrawShotTransition(*draw, sequence, s, timeOriginX, shotY, kRowHeight);
        if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            hoveredShot = &s;
            hoveredLocalTime =
                std::clamp((io.MousePos.x - timeOriginX) / zoom_ - s.startTimeSeconds, 0.0f, s.durationSeconds);
        }
        for (const auto& key : s.camera.keys) {
            if (!s.camera.timing.keys.empty() || key.time < s.sourceOffsetSeconds ||
                key.time > s.sourceOffsetSeconds + s.durationSeconds)
                continue;
            const float px = timeOriginX + (s.startTimeSeconds + key.time - s.sourceOffsetSeconds) * zoom_;
            draw->AddLine({px, shotY + kRowHeight - 8}, {px, shotY + kRowHeight - 3}, IM_COL32(173, 231, 247, 220));
        }
    }
    DrawShotHoverPreview(scene, sequence, hoveredShot, hoveredLocalTime);
    const float channelY = origin.y + kRuler + row * kRowHeight;
    changed |= DrawChannelLanes(scene, sequence, origin.x, channelY, width, kLabelWidth, kRowHeight);
    for (const auto& channel : CollectChannels(scene, sequence, true)) {
        if (!channel.visible)
            continue;
        lane(channel.label);
        (void)channel;
    }
    for (auto& track : sequence.objectTracks) {
        const float y = lane(track.name);
        changed |= DrawObjectTrackLane(scene, sequence, track, timeOriginX, y, width - kLabelWidth, kRowHeight);
    }
    // 音声の片段：淡入・淡出の傾きを重ね、上の角の小さな四角を左右へ引いて長さを変える（0.01 秒刻み）。
    const auto fades = [&](PresentationCueClip& cue, float y) {
        bool edited = false;
        // 片段を縮めた後も、淡入と淡出が片段の中に収まるように縮める。
        if (const float sum = cue.fadeInSeconds + cue.fadeOutSeconds; sum > cue.durationSeconds && sum > 0.0f) {
            const float scale = cue.durationSeconds / sum;
            cue.fadeInSeconds *= scale;
            cue.fadeOutSeconds *= scale;
            edited = true;
        }
        const float x0 = timeOriginX + cue.startSeconds * zoom_, x1 = x0 + (std::max)(10.0f, cue.durationSeconds * zoom_);
        const float top = y + 3, bottom = y + kRowHeight - 3;
        const float inX = x0 + cue.fadeInSeconds * zoom_, outX = x1 - cue.fadeOutSeconds * zoom_;
        const ImU32 shade = IM_COL32(10, 22, 16, 130), ramp = IM_COL32(214, 240, 226, 210);
        if (inX > x0 + 0.5f) {
            draw->AddTriangleFilled({x0, top}, {inX, top}, {x0, bottom}, shade);
            draw->AddLine({x0, bottom}, {inX, top}, ramp, 1.5f);
        }
        if (outX < x1 - 0.5f) {
            draw->AddTriangleFilled({outX, top}, {x1, top}, {x1, bottom}, shade);
            draw->AddLine({outX, top}, {x1, bottom}, ramp, 1.5f);
        }
        ImGui::PushID("cueFade");
        ImGui::PushID(static_cast<int>(cue.id));
        for (int side = 0; side < 2; ++side) {
            const float hx = side == 0 ? inX : outX;
            ImGui::SetCursorScreenPos({hx - 5.0f, top - 1.0f});
            ImGui::InvisibleButton(side == 0 ? "in" : "out", {10.0f, 10.0f});
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SetTooltip("%s  %.2f s", TEXT::Localize(side == 0 ? "cine.fadeIn" : "cine.fadeOut").c_str(),
                                  side == 0 ? cue.fadeInSeconds : cue.fadeOutSeconds);
            }
            if (ImGui::IsItemActivated()) {
                selectedId_ = cue.id;
                selectionKind_ = SelectionKind::Cue;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                const float t = std::round((io.MousePos.x - x0) / zoom_ * 100.0f) / 100.0f;
                if (side == 0)
                    cue.fadeInSeconds = std::clamp(t, 0.0f, cue.durationSeconds - cue.fadeOutSeconds);
                else
                    cue.fadeOutSeconds = std::clamp(cue.durationSeconds - t, 0.0f, cue.durationSeconds - cue.fadeInSeconds);
                edited = true;
            }
            draw->AddRectFilled({hx - 3.5f, top}, {hx + 3.5f, top + 7.0f}, hot ? IM_COL32(255, 255, 255, 255) : ramp, 1.5f);
        }
        ImGui::PopID();
        ImGui::PopID();
        return edited;
    };
    for (auto& cue : sequence.presentationCueTrack.clips) {
        const auto y =
            lane(TEXT::Localize(cue.kind == PresentationCueKind::Vfx ? "ui.assetBrowser.typeVfx" : "cine.sound"));
        const float oldStart = cue.startSeconds, oldEnd = cue.startSeconds + cue.durationSeconds;
        clip(cue.id, SelectionKind::Cue, cue.startSeconds, cue.durationSeconds, cue.name.c_str(), y,
             cue.kind == PresentationCueKind::Vfx ? IM_COL32(143, 78, 48, 255) : IM_COL32(51, 116, 89, 255));
        if (cue.kind != PresentationCueKind::Audio)
            continue;
        // 左端のトリムは音の素材の始まりを移す（镜头と同じ。片段の移動は素材を変えない）。素材の頭より前へは伸ばさない。
        if (draggingKind_ == SelectionKind::Cue && draggingId_ == cue.id && dragPart_ == -1 && cue.startSeconds != oldStart) {
            const float pitch = (std::max)(0.25f, cue.pitch);
            const float start = (std::max)(cue.startSeconds, oldStart - cue.sourceOffsetSeconds / pitch);
            cue.sourceOffsetSeconds = (std::max)(0.0f, cue.sourceOffsetSeconds + (start - oldStart) * pitch);
            cue.startSeconds = start;
            cue.durationSeconds = oldEnd - start;
        }
        changed |= fades(cue, y);
    }
    for (auto& effect : sequence.screenEffects) {
        const auto y = lane(TEXT::Localize("cine.screenFx"));
        const auto name = effect.name.empty() ? TEXT::Localize("cine.screenFx") : effect.name;
        clip(effect.id, SelectionKind::Effect, effect.start, effect.duration, name.c_str(), y,
             IM_COL32(105, 71, 146, 255));
    }
    // 文字段：時間の重ならない行へ詰めて並べる（行の名前は最初の行だけ）。样式の無い片段は引擎が描かないので沈んだ色。
    std::vector<float> textRowY;
    for (size_t index = 0; index < textRowCount; ++index)
        textRowY.push_back(lane(index == 0 ? TEXT::Localize("cine.textTrack") : std::string{}));
    for (auto& text : sequence.textTrack.clips) {
        const std::string label = SEQUENCER::GetSequenceTextLabel(text);
        clip(text.id, SelectionKind::Text, text.startSeconds, text.durationSeconds, label.c_str(),
             textRowY[textRows_[text.id]], text.style.IsValid() ? IM_COL32(156, 110, 40, 255) : IM_COL32(96, 86, 70, 255));
    }
    const float textAddY = lane(TEXT::Localize("cine.textAdd"));
    draw->AddText({timeOriginX + 10, textAddY + 7}, IM_COL32(95, 110, 126, 255),
                  TEXT::Localize("cine.textAddHint").c_str());
    ImGui::SetCursorScreenPos({timeOriginX, textAddY});
    ImGui::InvisibleButton("addText", {(std::max)(1.0f, width - kLabelWidth), kRowHeight});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        changed |= AddTextClip(sequence,
                               SnapTimelineTime(sequence, (std::max)(0.0f, (io.MousePos.x - timeOriginX) / zoom_), 0));
    for (const auto& eventName : eventLanes) {
        const auto y = lane(eventName);
        for (auto& event : sequence.eventTrack.clips)
            if (event.name == eventName)
                clip(event.id, SelectionKind::Event, event.startSeconds, event.durationSeconds, event.name.c_str(), y,
                     IM_COL32(46, 120, 128, 255));
    }
    // 事件段を足す行：ダブルクリックした時刻に置く。名前はゲームのプロジェクトが読む事件の名前に直す。
    const float eventAddY = lane("＋ 事件");
    draw->AddText({timeOriginX + 10, eventAddY + 7}, IM_COL32(95, 110, 126, 255), "双击添加事件段（游戏项目按名字读取）");
    ImGui::SetCursorScreenPos({timeOriginX, eventAddY});
    ImGui::InvisibleButton("addEvent", {(std::max)(1.0f, width - kLabelWidth), kRowHeight});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
        sequence.eventTrack.clips.size() < SEQUENCER::kMaximumSequenceEvents) {
        SEQUENCER::SequenceEventClip event;
        for (const auto& e : sequence.eventTrack.clips)
            event.id = (std::max)(event.id, e.id + 1);
        event.startSeconds = SnapTimelineTime(sequence, (std::max)(0.0f, (io.MousePos.x - timeOriginX) / zoom_), 0);
        sequence.eventTrack.clips.push_back(event);
        selectionKind_ = SelectionKind::Event;
        selectedId_ = event.id;
        changed = true;
    }
    const float dropY = lane(TEXT::Localize("cine.dropAssets"));
    draw->AddText({timeOriginX + 10, dropY + 7}, IM_COL32(95, 110, 126, 255),
                  TEXT::Localize("cine.dropAssetsHint").c_str());
    ImGui::SetCursorScreenPos({timeOriginX, dropY});
    ImGui::InvisibleButton("dropCue", {(std::max)(1.0f, width - kLabelWidth), kRowHeight});
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
            cue.startSeconds = SnapTimelineTime(sequence, (io.MousePos.x - timeOriginX) / zoom_, 0);
            const char* bytes = static_cast<const char*>(payload->Data);
            if (bytes[payload->DataSize - 1] != '\0')
                return;
            const std::string data(bytes, static_cast<size_t>(payload->DataSize - 1));
            if (kind == PresentationCueKind::Vfx) {
                const auto* record = scene.GetAssetDatabase().FindByGuid(AssetGuid{data});
                if (record && record->type == AssetType::Audio) {
                    // 資産の一覧から落とした音声は、その文件の場所で音の合図にする（演出は Assets/Audio の下だけを鳴らす）。
                    auto source = record->sourcePath;
                    if (source.is_absolute())
                        source = source.lexically_relative(scene.GetAssetDatabase().GetProjectRoot());
                    const std::string path = TEXT::PathToUtf8(source.lexically_normal());
                    if (!path.starts_with("Assets/Audio/")) {
                        pendingStatusMessage_ = TEXT::Localize("cine.audioFolderRequired");
                        return;
                    }
                    cue.kind = PresentationCueKind::Audio;
                    cue.soundPath = path;
                    cue.name = GetAssetRecordDisplayName(*record);
                    if (const float length = GetPresentationCueSoundSeconds(path); length > 0.0f)
                        cue.durationSeconds = length;
                } else if (!record || record->type != AssetType::VfxEffect) {
                    pendingStatusMessage_ = TEXT::Localize("cine.vfxRequired");
                    return;
                } else {
                    cue.vfxAsset.value = data;
                    cue.name = GetAssetRecordDisplayName(*record);
                }
            } else {
                cue.soundPath = data;
                // 置いた音は素材の長さぶんの片段にする。
                if (const float length = GetPresentationCueSoundSeconds(data); length > 0.0f)
                    cue.durationSeconds = length;
            }
            sequence.presentationCueTrack.clips.push_back(cue);
            changed = true;
            selectionKind_ = SelectionKind::Cue;
            selectedId_ = cue.id;
        };
        accept(ImGui::AcceptDragDropPayload(kAssetGuidPayload), PresentationCueKind::Vfx);
        accept(ImGui::AcceptDragDropPayload("HIKARI_SEQUENCE_AUDIO"), PresentationCueKind::Audio);
        ImGui::EndDragDropTarget();
    }
    const float playheadX = timeOriginX + cursor_.GetTimeSeconds() * zoom_;
    draw->AddLine({playheadX, origin.y}, {playheadX, origin.y + height}, IM_COL32(105, 220, 230, 255), 2);
    draw->AddTriangleFilled({playheadX - 6, origin.y}, {playheadX + 6, origin.y}, {playheadX, origin.y + 8},
                            IM_COL32(105, 220, 230, 255));
    // 行の名前は左端に固定して描く（横へ送っても読める）。
    const float pinnedX = ImGui::GetWindowPos().x;
    draw->AddRectFilled({pinnedX, origin.y}, {pinnedX + kLabelWidth, origin.y + height}, IM_COL32(19, 25, 33, 255));
    draw->AddLine({pinnedX + kLabelWidth, origin.y}, {pinnedX + kLabelWidth, origin.y + height},
                  IM_COL32(45, 56, 68, 255));
    for (const auto& [y, name] : labels) {
        draw->PushClipRect({pinnedX + 4, y}, {pinnedX + kLabelWidth - 4, y + kRowHeight}, true);
        draw->AddText({pinnedX + 10, y + 7}, IM_COL32(155, 172, 192, 255), name.c_str());
        draw->PopClipRect();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, height});
    // 時間轴の操作：Delete で片段を消す、S で镜头を再生位置で分ける、Ctrl+D で複製、K で姿勢键を置く、
    // [ ] で前後の姿勢键へ、空白で再生と一時停止。
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, canvasFocused))
        changed |= DeleteSelectedClip(sequence);
    if (!io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_S, canvasFocused))
        changed |= SplitSelectedShot(sequence);
    if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_D, canvasFocused))
        changed |= DuplicateSelectedShot(sequence);
    if (!io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_K, canvasFocused))
        changed |= selectionKind_ == SelectionKind::Object ? AddObjectKeyAtCursor(scene, sequence)
                                                           : AddPoseKeyAtCursor(scene, sequence);
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_LeftBracket, canvasFocused))
        SeekPoseKey(sequence, -1);
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_RightBracket, canvasFocused))
        SeekPoseKey(sequence, 1);
    ImGui::EndChild();
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
