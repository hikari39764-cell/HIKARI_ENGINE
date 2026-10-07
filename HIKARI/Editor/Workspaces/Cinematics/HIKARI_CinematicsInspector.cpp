#include "Editor/Widgets/HIKARI_CameraMotionWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsPresentation.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawInspector(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    const bool narrow = ImGui::GetContentRegionAvail().x < 300.0f * ImGui::GetFontSize() / 16.0f;
    const auto fieldLabel = [narrow](const char* label) {
        if (!narrow)
            return std::string(label);
        const std::string_view value(label);
        const auto suffix = value.find("##");
        ImGui::TextUnformatted(label, label + (suffix == std::string_view::npos ? value.size() : suffix));
        ImGui::SetNextItemWidth(-1);
        return "##" + std::string(label);
    };
    const auto text = [&](const char* label, std::string& value) {
        std::array<char, 1024> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        if (ImGui::InputText(fieldLabel(label).c_str(), buffer.data(), buffer.size())) {
            value = buffer.data();
            return true;
        }
        return false;
    };
    const auto binding = [&](const char* label, SequenceBindingId& id) {
        const auto* current = FindSequenceBinding(sequence.bindings, id);
        bool result = false;
        if (ImGui::BeginCombo(fieldLabel(label).c_str(),
                              current ? current->name.c_str() : TEXT::Localize("cine.worldSpace").c_str())) {
            if (ImGui::Selectable(TEXT::LocalizeWindow("cine.worldSpace", "无 / 世界空间").c_str(), !id.IsValid())) {
                id = {};
                result = true;
            }
            for (const auto& b : sequence.bindings)
                if (ImGui::Selectable(b.name.c_str(), b.id == id)) {
                    id = b.id;
                    result = true;
                }
            ImGui::EndCombo();
        }
        return result;
    };
    if (auto* shot = SelectedShot(sequence)) {
        PanelTitle(TEXT::Localize("cine.shot").c_str(), shot->camera.name.c_str());
        changed |= text(TEXT::Localize("ui.input.name").c_str(), shot->camera.name);
        changed |= ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("ui.vfx.text.Start", "开始时间").c_str()).c_str(),
                                    &shot->startTimeSeconds, 0.02f, 0, 10000);
        changed |= ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("cine.shotDuration", "镜头时长").c_str()).c_str(),
                                    &shot->durationSeconds, 0.02f, 0.1f, 10000);
        if (IconTextButton(EditorGlyph::Duplicate, TEXT::Localize("ui.materialGraph.copy").c_str(), "duplicateShot")) {
            auto copy = *shot;
            copy.id = AllocateCameraCutClipId(sequence.cameraCutTrack);
            copy.startTimeSeconds += copy.durationSeconds;
            copy.camera.name += " Copy";
            selectedId_ = copy.id;
            sequence.cameraCutTrack.clips.push_back(std::move(copy));
            return true;
        }
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Delete, "deleteShot", EditorButtonTone::Danger, {26, 26},
                       TEXT::Localize("cine.deleteShot").c_str())) {
            return DeleteSelectedClip(sequence);
        }
        if (!shot->camera.owned) {
            ImGui::TextWrapped(TEXT::Localize("cine.convertHint").c_str());
            if (ImGui::Button(TEXT::LocalizeWindow("cine.convertCamera", "转换为演出内镜头（60 fps 采样）").c_str())) {
                SceneObjectId id;
                Camera3D camera;
                if (previewBindings_.Resolve(sequence.bindings, shot->cameraBindingId, id) &&
                    scene.TryResolveCameraObjectView(id, 16.0f / 9, camera)) {
                    for (auto& channel : shot->camera.channels)
                        channel.keys.clear();
                    shot->camera.nearClip = camera.GetNearZ();
                    shot->camera.farClip = camera.GetFarZ();
                    const int count = static_cast<int>(std::ceil(shot->durationSeconds * 60));
                    for (int frame = 0; frame <= count; ++frame) {
                        const float local = (std::min)(frame / 60.0f, shot->durationSeconds);
                        CinematicCameraEvaluation evaluation;
                        evaluation.shotId = shot->id;
                        evaluation.cameraObjectId = id;
                        evaluation.transform = EvaluateCameraTransformTrack(
                            sequence.cameraTransformTrack, shot->cameraBindingId, shot->startTimeSeconds + local);
                        evaluation.lens = EvaluateCameraLensTrack(sequence.cameraLensTrack, shot->cameraBindingId,
                                                                  shot->startTimeSeconds + local);
                        auto sample = camera;
                        if (evaluation.HasCameraAnimation())
                            BuildEvaluatedCinematicCamera(evaluation, camera, 16.0f / 9, sample);
                        auto pose = CaptureShotPose(sample, local);
                        SetShotPose(shot->camera, pose, true);
                        shot->camera.nearClip = (std::min)(shot->camera.nearClip, sample.GetNearZ());
                        shot->camera.farClip = (std::max)(shot->camera.farClip, sample.GetFarZ());
                    }
                    for (auto& channel : shot->camera.channels)
                        for (size_t i = 0; i < channel.keys.size(); ++i) {
                            auto& key = channel.keys[i];
                            if (i)
                                key.inSlope =
                                    (key.value - channel.keys[i - 1].value) / (key.time - channel.keys[i - 1].time);
                            if (i + 1 < channel.keys.size())
                                key.outSlope =
                                    (channel.keys[i + 1].value - key.value) / (channel.keys[i + 1].time - key.time);
                        }
                    shot->camera.owned = true;
                    changed = true;
                }
            }
        }
        if (shot->camera.owned &&
            ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.composition", "构图与关键帧").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= binding(TEXT::Localize("cine.anchor").c_str(), shot->camera.anchor);
            changed |= binding(TEXT::Localize("cine.lookAt").c_str(), shot->camera.lookAt);
            changed |=
                ImGui::DragFloat3(fieldLabel(TEXT::LocalizeWindow("cine.lookOffset", "注视偏移").c_str()).c_str(),
                                  &shot->camera.targetOffset.x, 0.02f);
            const float local =
                std::clamp(cursor_.GetTimeSeconds() - shot->startTimeSeconds, 0.0f, shot->durationSeconds);
            if (IconTextButton(EditorGlyph::Keyframe, TEXT::Localize("cine.addKey").c_str(), "addPose",
                               EditorButtonTone::Quiet, {}, TEXT::Localize("cine.addPose").c_str())) {
                SetShotPose(shot->camera, EvaluateShotPose(shot->camera, local), true);
                changed = true;
            }
            const bool table = !narrow && ImGui::BeginTable("cameraChannels", 2, ImGuiTableFlags_SizingStretchProp);
            if (table) {
                ImGui::TableSetupColumn("labels", ImGuiTableColumnFlags_WidthFixed, 86);
                ImGui::TableSetupColumn("values", ImGuiTableColumnFlags_WidthStretch);
            }
            if (narrow || table) {
                for (size_t i = 0; i < shot->camera.channels.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (table) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(TEXT::Localize(kCinematicChannelKeys[i]).c_str());
                    if (table)
                        ImGui::TableSetColumnIndex(1);
                    changed |= DrawCurve(shot->camera.channels[i], local, kShotChannelNames[i]);
                    ImGui::PopID();
                }
            }
            if (table)
                ImGui::EndTable();
        }
        if (shot->camera.owned &&
            ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.motionChain", "运动效果链").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawCameraMotionStack(
                shot->camera.motion, std::max(0.0f, cursor_.GetTimeSeconds() - shot->startTimeSeconds),
                [this](auto& parameter, float time, const auto& channel) {
                    return DrawCurve(parameter, time, channel);
                },
                false);
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.shotFx", "镜头画面效果").c_str()))
            changed |= DrawEffects(shot->effects, (std::max)(0.0f, cursor_.GetTimeSeconds() - shot->startTimeSeconds));
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.transition", "镜头衔接").c_str())) {
            bool blend = shot->transition.mode == CameraCutTransitionMode::EaseInOut;
            if (ImGui::Checkbox(TEXT::LocalizeWindow("cine.blendMotion", "平滑运镜过渡").c_str(), &blend)) {
                shot->transition.mode = blend ? CameraCutTransitionMode::EaseInOut : CameraCutTransitionMode::Cut;
                changed = true;
            }
            if (blend)
                changed |=
                    ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("cine.blendDuration", "过渡时间").c_str()).c_str(),
                                     &shot->transition.durationSeconds, 0.02f, 0, shot->durationSeconds);
            ImGui::TextDisabled(TEXT::Localize("cine.fadeHint").c_str());
        }
    } else if (selectionKind_ == SelectionKind::Cue) {
        for (auto& cue : sequence.presentationCueTrack.clips)
            if (cue.id == selectedId_) {
                PanelTitle(cue.kind == PresentationCueKind::Vfx ? TEXT::Localize("cine.vfxClip").c_str()
                                                                : TEXT::Localize("cine.soundClip").c_str());
                changed |= text(TEXT::Localize("ui.input.name").c_str(), cue.name);
                changed |=
                    ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("ui.vfx.text.Start", "开始").c_str()).c_str(),
                                     &cue.startSeconds, 0.02f, 0, 10000);
                changed |= ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("cine.duration", "时长").c_str()).c_str(),
                                            &cue.durationSeconds, 0.02f, 0.01f, 10000);
                changed |= binding(TEXT::Localize("cine.boundObject").c_str(), cue.targetBinding);
                if (cue.kind == PresentationCueKind::Vfx) {
                    AssetFieldOptions options;
                    options.label = TEXT::Localize("menu.workspace.vfx");
                    options.requiredType = AssetType::VfxEffect;
                    changed |= DrawAssetField(&scene.GetAssetDatabase(), options, cue.vfxAsset.value);
                } else {
                    changed |= text(TEXT::Localize("cine.audioFile").c_str(), cue.soundPath);
                    changed |= ImGui::SliderFloat(TEXT::LocalizeWindow("cine.volume", "音量").c_str(),
                                                  &cue.audio.volume, 0, 1);
                }
                if (ImGui::Button(TEXT::LocalizeWindow("cine.deleteClip", "删除片段").c_str())) {
                    return DeleteSelectedClip(sequence);
                }
                break;
            }
    } else if (selectionKind_ == SelectionKind::Effect) {
        PanelTitle(TEXT::Localize("cine.sequenceFx").c_str(), TEXT::Localize("cine.outputOnly").c_str());
        changed |= DrawEffects(sequence.screenEffects, cursor_.GetTimeSeconds());
    } else {
        PanelTitle(TEXT::Localize("cine.sequence").c_str());
        changed |= text(TEXT::Localize("ui.input.name").c_str(), sequence.name);
        changed |= ImGui::DragFloat(fieldLabel(TEXT::LocalizeWindow("cine.duration", "时长").c_str()).c_str(),
                                    &sequence.durationSeconds, 0.1f, 1, 10000);
        ImGui::TextWrapped(TEXT::Localize("cine.editHint").c_str());
    }
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
