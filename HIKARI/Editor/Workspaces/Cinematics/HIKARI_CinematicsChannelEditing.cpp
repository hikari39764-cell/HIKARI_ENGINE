#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsPresentation.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
std::vector<CinematicsWorkspaceController::Channel> CinematicsWorkspaceController::CollectChannels(
    DocumentSceneBase& scene, CinematicSequence& sequence, bool visibleOnly) {
    std::vector<Channel> result;
    const uint64_t owner = (selectedId_ << 2) | static_cast<uint64_t>(selectionKind_);
    if (channelOwner_ != owner) {
        channelOwner_ = owner;
        const float fps = curveView_.fps;
        auto clipboard = std::move(curveView_.editing.clipboard);
        const auto easing = curveView_.editing.easing;
        curveView_ = {};
        curveView_.editing.clipboard = std::move(clipboard);
        curveView_.editing.easing = easing;
        curveView_.fps = fps;
        curveView_.activeChannel = "timing";
        curveView_.speedGraph = true;
    }
    const auto effects = [&](auto& items, float start) {
        for (auto& effect : items) {
            const auto name =
                effect.name.empty() ? TEXT::Localize(kCinematicEffectKeys[static_cast<int>(effect.kind)]) : effect.name;
            const auto prefix = "effect/" + std::to_string(effect.id);
            result.push_back({prefix + "/amount", name + " · " + TEXT::Localize("cine.amount"), &effect.amount,
                              start + effect.start, effect.duration});
            result.push_back({prefix + "/weight", name + " · " + TEXT::Localize("cine.weight"), &effect.weight,
                              start + effect.start, effect.duration});
        }
    };
    if (auto* shot = SelectedShot(sequence)) {
        const float start = shot->startTimeSeconds - shot->sourceOffsetSeconds;
        const float end = shot->sourceOffsetSeconds + shot->durationSeconds;
        // 表示を開くだけではキーを生成・保存しない。実際の編集があった時だけ timing へ反映する。
        timingCurve_ = shot->camera.timing;
        if (timingCurve_.keys.empty())
            timingCurve_.keys = {{0, 0, 1, 1}, {end, end, 1, 1}};
        result.push_back({"timing", "整体运镜 · 节奏", &timingCurve_, start, end});
        auto& timing = result.back();
        timing.locked = {0, timingCurve_.keys.size() - 1};
        timing.monotonic = true;
        timing.valueScale = 100.f / (std::max)(.001f, shot->durationSeconds);
        timing.valueOffset = -shot->sourceOffsetSeconds * timing.valueScale;
        timing.unit = "%（速度：%/s）";
        SEQUENCER::ShotCamera resolved;
        if (SEQUENCER::ResolveSequenceShotCamera(sequence, previewBindings_, *shot, scene.GetWorld(),
                                                 scene.GetCameraDirector(), resolved)) {
            constexpr const char* names[]{"位置 X", "位置 Y", "位置 Z", "视野 FOV"};
            for (unsigned i = 0; i < poseCurves_.size(); ++i) {
                poseCurves_[i] = SEQUENCER::GetShotPoseCurve(resolved, i);
                result.push_back({"pose/" + std::to_string(i), names[i], &poseCurves_[i], start, end, 0});
                auto& c = result.back();
                c.unit = i == 3 ? "度（速度：度/s）" : "m（速度：m/s）";
                if (!curveView_.allowStationOffsets)
                    for (size_t key = 0; key < shot->camera.keys.size(); ++key)
                        if (shot->camera.keys[key].reference.IsValid())
                            c.locked.push_back(key);
            }
        }
        if (shot->camera.lookAt.IsValid())
            result.push_back({"aimWeight", TEXT::Localize("cine.aimWeight"), &shot->camera.aimWeight, start, end});
        if (shot->camera.rail.points.size() >= 2)
            result.push_back({"railT", TEXT::Localize("cine.railProgress"), &shot->camera.railT, start, end});
        for (auto& motion : shot->camera.motion) {
            const auto name = TEXT::Localize(SEQUENCER::kCameraMotionDescriptors[static_cast<size_t>(motion.kind)].key);
            const auto prefix = "motion/" + std::to_string(motion.id);
            const float duration = motion.duration > 0 ? motion.duration : (std::max)(0.f, end - motion.start);
            result.push_back({prefix + "/amount", name + " · " + TEXT::Localize("cine.amount"), &motion.amount,
                              start + motion.start, duration});
            result.push_back({prefix + "/weight", name + " · " + TEXT::Localize("cine.weight"), &motion.weight,
                              start + motion.start, duration});
        }
        effects(shot->effects, start);
    } else if (selectionKind_ == SelectionKind::Effect)
        for (auto& effect : sequence.screenEffects)
            if (effect.id == selectedId_) {
                const auto prefix = "effect/" + std::to_string(effect.id);
                result.push_back(
                    {prefix + "/amount", TEXT::Localize("cine.amount"), &effect.amount, effect.start, effect.duration});
                result.push_back(
                    {prefix + "/weight", TEXT::Localize("cine.weight"), &effect.weight, effect.start, effect.duration});
            }
    for (auto& c : result) {
        c.visible =
            !visibleOnly || !animatedChannelsOnly_ || !c.curve->keys.empty() || c.id == curveView_.activeChannel;
        // 時間写像の素材キーを主時間軸の時刻として表示しない。曲線モードでは素材時間を明記する。
        if (visibleOnly)
            if (const auto* shot = SelectedShot(sequence);
                shot && !shot->camera.timing.keys.empty() && c.id != "timing")
                c.visible = c.id.starts_with("effect/");
    }
    return result;
}
void CinematicsWorkspaceController::WriteChannelEdits(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                      const std::array<ScalarCurve, 4>& beforePose,
                                                      const ScalarCurve& beforeTiming) {
    auto* shot = SelectedShot(sequence);
    if (!shot)
        return;
    if (poseCurves_ != beforePose) {
        SEQUENCER::ShotCamera resolved;
        if (SEQUENCER::ResolveSequenceShotCamera(sequence, previewBindings_, *shot, scene.GetWorld(),
                                                 scene.GetCameraDirector(), resolved))
            ApplyResolvedShotPoseCurves(shot->camera, resolved, poseCurves_);
    }
    if (timingCurve_ != beforeTiming)
        shot->camera.timing = timingCurve_;
}
bool CinematicsWorkspaceController::DrawChannelToolbar(DocumentSceneBase& scene, CinematicSequence& sequence) {
    auto channels = CollectChannels(scene, sequence, true);
    if (channels.empty())
        return false;
    const auto beforePose = poseCurves_;
    const auto beforeTiming = timingCurve_;
    const bool changed = DrawCurveEditorToolbar(channels, cursor_.GetTimeSeconds(), curveView_, false);
    if (changed)
        WriteChannelEdits(scene, sequence, beforePose, beforeTiming);
    return changed;
}
bool CinematicsWorkspaceController::DrawChannelLanes(DocumentSceneBase& scene, CinematicSequence& sequence, float x,
                                                     float y, float width, float labelWidth, float rowHeight) {
    auto channels = CollectChannels(scene, sequence, true);
    const auto count = std::count_if(channels.begin(), channels.end(), [](const auto& c) { return c.visible; });
    if (!count)
        return false;
    const auto beforePose = poseCurves_;
    const auto beforeTiming = timingCurve_;
    ImGui::SetCursorScreenPos({x, y});
    const bool changed =
        DrawCurveEditorCanvas(channels, cursor_.GetTimeSeconds(), curveView_,
                              {{width, static_cast<float>(count) * rowHeight}, true, labelWidth, rowHeight, 0, zoom_});
    if (changed)
        WriteChannelEdits(scene, sequence, beforePose, beforeTiming);
    return changed;
}
bool CinematicsWorkspaceController::DrawChannelCurve(DocumentSceneBase& scene, CinematicSequence& sequence) {
    auto channels = CollectChannels(scene, sequence);
    if (channels.empty()) {
        ImGui::TextDisabled("选择一个镜头以编辑关键帧");
        return false;
    }
    auto found =
        std::find_if(channels.begin(), channels.end(), [&](const auto& c) { return c.id == curveView_.activeChannel; });
    if (found == channels.end()) {
        found = channels.begin();
        curveView_.activeChannel = found->id;
    }
    ImGui::SetNextItemWidth(230);
    if (ImGui::BeginCombo("##curveChannel", found->label.c_str())) {
        for (const auto& c : channels)
            if (ImGui::Selectable(c.label.c_str(), c.id == curveView_.activeChannel)) {
                curveView_.activeChannel = c.id;
                curveView_.fitted = false;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("叠加同类通道", &curveView_.overlay))
        curveView_.fitted = false;
    ImGui::SameLine();
    ImGui::Checkbox("允许本演出机位偏移", &curveView_.allowStationOffsets);
    found =
        std::find_if(channels.begin(), channels.end(), [&](const auto& c) { return c.id == curveView_.activeChannel; });
    for (auto& c : channels)
        c.visible = c.id == found->id || (curveView_.overlay && c.unit == found->unit && c.offset == found->offset);
    ImGui::TextDisabled(
        "%s · %s", found->id == "timing" ? "播放时间 → 运镜进度；起终点受保护" : "素材时间 → 属性；位置关键帧共用时间",
        found->unit.c_str());
    const auto beforePose = poseCurves_;
    const auto beforeTiming = timingCurve_;
    float playhead = cursor_.GetTimeSeconds();
    if (const auto* shot = SelectedShot(sequence); shot && found->id != "timing" && !found->id.starts_with("effect/"))
        playhead = found->offset + ShotSourceTime(*shot);
    bool changed = DrawCurveEditorToolbar(channels, playhead, curveView_, true);
    changed |= DrawCurveEditorCanvas(
        channels, playhead, curveView_,
        {{ImGui::GetContentRegionAvail().x, (std::max)(140.f, ImGui::GetContentRegionAvail().y)}});
    if (curveView_.seekTime) {
        // 素材から再生時刻への逆写像が曖昧な場合は、時間曲線側でだけスクラブする。
        const auto* shot = SelectedShot(sequence);
        if (!shot || shot->camera.timing.keys.empty() || found->id == "timing") {
            cursor_.Pause();
            cursor_.Seek(std::clamp(*curveView_.seekTime, 0.f, sequence.durationSeconds));
            previewEnabled_ = true;
        }
        curveView_.seekTime.reset();
    }
    if (changed)
        WriteChannelEdits(scene, sequence, beforePose, beforeTiming);
    return changed;
}
} // namespace HIKARI::EDITOR
