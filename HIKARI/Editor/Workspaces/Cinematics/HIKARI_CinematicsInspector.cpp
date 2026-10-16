#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
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
#include <numbers>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
namespace {
constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

// 姿勢の向きを俯仰・偏航・傾き（度）で表す。俯仰は上向きが正、偏航は +Z から +X への回り、傾きは水平に構えた時の
// 上向きからの回り。
MATH::Vec3 PoseAngles(const SEQUENCER::ShotPose& pose) {
    const MATH::Vec3 forward = SEQUENCER::ShotPoseForward(pose);
    const MATH::Vec3 up = SEQUENCER::ShotPoseUp(pose);
    const auto level = MATH::BuildViewBasisRH(forward, {0.0f, 1.0f, 0.0f});
    return {std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * kDegrees, std::atan2(forward.x, forward.z) * kDegrees,
            std::atan2(MATH::Dot(up, level.right), MATH::Dot(up, level.up)) * kDegrees};
}

MATH::Quat RotationFromAngles(const MATH::Vec3& degrees) {
    const float pitch = degrees.x / kDegrees, yaw = degrees.y / kDegrees, roll = degrees.z / kDegrees;
    const MATH::Vec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const auto level = MATH::BuildViewBasisRH(forward, {0.0f, 1.0f, 0.0f});
    return MATH::QuatFromForwardUp(forward, level.up * std::cos(roll) + level.right * std::sin(roll));
}

// 縦の画角（度）を 35mm 判（縦 24mm）の焦点距離へ。
float FocalLength(float fovDegrees) { return 12.0f / std::tan(fovDegrees * 0.5f / kDegrees); }
} // namespace

bool CinematicsWorkspaceController::DrawInspector(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                  EditorOperationRegistry& operations) {
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
            if (ImGui::Selectable(TEXT::Localize("cine.worldSpace").c_str(), !id.IsValid())) {
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
        changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("ui.vfx.text.Start").c_str()).c_str(),
                                    &shot->startTimeSeconds, 0.02f, 0, 10000, "%.2f s");
        changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.shotDuration").c_str()).c_str(),
                                    &shot->durationSeconds, 0.02f, 0.1f, 10000, "%.2f s");
        if (IconTextButton(EditorGlyph::Duplicate, TEXT::Localize("ui.materialGraph.copy").c_str(), "duplicateShot"))
            return DuplicateSelectedShot(sequence);
        ImGui::SameLine();
        ImGui::BeginDisabled(ShotLocalTime(*shot) <= 0.0f || ShotLocalTime(*shot) >= shot->durationSeconds ||
                             (shot->transition.mode == CameraCutTransitionMode::EaseInOut &&
                              ShotLocalTime(*shot) < shot->transition.durationSeconds));
        const bool split = IconTextButton(EditorGlyph::Transform, TEXT::Localize("cine.splitShot").c_str(), "splitShot",
                                          EditorButtonTone::Quiet, {}, TEXT::Localize("cine.splitShotTip").c_str());
        ImGui::EndDisabled();
        if (split)
            return SplitSelectedShot(sequence);
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Delete, "deleteShot", EditorButtonTone::Danger, {26, 26},
                       TEXT::Localize("cine.deleteShot").c_str()))
            return DeleteSelectedClip(sequence);
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.poseSection").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const float local = ShotLocalTime(*shot);
            ShotPose pose;
            if (!EvaluateSequenceShotPose(sequence, previewBindings_, *shot, scene.GetWorld(),
                                          scene.GetCameraDirector(), ShotSourceTime(*shot), pose)) {
                ImGui::TextWrapped("机位引用无法解析，请检查对象绑定。");
                return changed;
            }
            const ShotPose* key = FindShotPoseKey(shot->camera, ShotSourceTime(*shot));
            const bool shared = SharedPoseAtCursor(*shot);
            if (shared) {
                const auto reference = key ? key->reference : shot->camera.base.reference;
                const auto* info = FindSequenceBinding(sequence.bindings, reference);
                ImGui::TextWrapped("共享机位：%s", info ? info->name.c_str() : "");
                SceneObjectId id;
                if (previewBindings_.Resolve(sequence.bindings, reference, id) && ImGui::Button("编辑共享机位"))
                    operations.Execute("gameplay_camera.reveal", {{"cameraId", id.value}});
                ImGui::TextWrapped("此端点引用场景机位。修改共享机位会更新所有连接的演出。");
            }
            if (key || shot->camera.keys.empty()) {
                SequenceBindingId reference = key ? key->reference : shot->camera.base.reference;
                if (binding("共享机位引用", reference)) {
                    // 解除は現在の絶対姿勢を保持し、選択時は共有機位のゼロ偏移へ合わせる。
                    auto assigned = pose;
                    assigned.reference = reference;
                    if (reference.IsValid()) {
                        assigned.position = {};
                        assigned.rotation = {};
                        assigned.fov = 0;
                    }
                    SetShotPose(shot->camera, assigned, key != nullptr);
                    return true;
                }
            }
            ImGui::BeginDisabled(shared);

            // 今の時刻の键の状態と、前後の键・键の追加と削除。
            if (shot->camera.keys.empty())
                ImGui::TextDisabled("%s", TEXT::Localize("cine.poseBase").c_str());
            else
                ImGui::TextDisabled(TEXT::Localize(key ? "cine.poseOnKey" : "cine.poseBetweenKeys").c_str(),
                                    static_cast<int>(shot->camera.keys.size()), local);
            if (IconButton(EditorGlyph::StepBackward, "previousPoseKey", EditorButtonTone::Quiet, {26, 26},
                           TEXT::Localize("cine.previousKey").c_str()))
                SeekPoseKey(sequence, -1);
            ImGui::SameLine();
            if (IconTextButton(EditorGlyph::Keyframe, TEXT::Localize(key ? "cine.updateKey" : "cine.addKey").c_str(),
                               "addPose", EditorButtonTone::Quiet, {}, TEXT::Localize("cine.addPose").c_str()))
                changed |= AddPoseKeyAtCursor(scene, sequence);
            ImGui::SameLine();
            ImGui::BeginDisabled(key == nullptr);
            if (IconButton(EditorGlyph::Delete, "deletePoseKey", EditorButtonTone::Quiet, {26, 26},
                           TEXT::Localize("cine.deleteKey").c_str()))
                changed |= RemovePoseKeyAtCursor(sequence);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (IconButton(EditorGlyph::StepForward, "nextPoseKey", EditorButtonTone::Quiet, {26, 26},
                           TEXT::Localize("cine.nextKey").c_str()))
                SeekPoseKey(sequence, 1);
            // 値を変えると、自動键か键がある時はこの時刻の键、無ければ基準の姿勢を書き換える。
            ShotPose edited = pose;
            const bool onRail = shot->camera.rail.points.size() >= 2;
            ImGui::BeginDisabled(onRail);
            if (ImGui::DragFloat3(fieldLabel(TEXT::Localize("cine.position").c_str()).c_str(), &edited.position.x,
                                  0.02f, 0.0f, 0.0f, "%.2f")) {
                WriteShotPose(sequence, *shot, edited, kPosePosition);
                changed = true;
            }
            ImGui::EndDisabled();
            if (onRail && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", TEXT::Localize("cine.positionOnRail").c_str());
            MATH::Vec3 angles = PoseAngles(pose);
            if (ImGui::DragFloat3(fieldLabel(TEXT::Localize("cine.rotationAngles").c_str()).c_str(), &angles.x, 0.2f,
                                  0.0f, 0.0f, "%.1f°")) {
                angles.x = std::clamp(angles.x, -89.5f, 89.5f);
                edited.rotation = RotationFromAngles(angles);
                WriteShotPose(sequence, *shot, edited, kPoseRotation);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::Localize("cine.rotationAnglesTip").c_str());
            char lens[48];
            std::snprintf(lens, sizeof(lens), "%%.1f° (%.0fmm)", FocalLength(pose.fov));
            if (ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.fov").c_str()).c_str(), &edited.fov, 0.1f, 1.0f,
                                 179.0f, lens)) {
                WriteShotPose(sequence, *shot, edited, kPoseFov);
                changed = true;
            }
            ImGui::EndDisabled();
            auto* editableKey = FindShotPoseKey(shot->camera, ShotSourceTime(*shot));
            auto& interpolation = editableKey ? editableKey->interpolation : shot->camera.base.interpolation;
            if (ImGui::BeginCombo("姿态路径插值", TEXT::EnumToName(kShotInterpolationNames, interpolation))) {
                for (const auto& mode : kShotInterpolationNames)
                    if (ImGui::Selectable(mode.name, mode.value == interpolation)) {
                        interpolation = mode.value;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button("应用路径插值到全部姿态")) {
                for (auto& k : shot->camera.keys) {
                    k.interpolation = interpolation;
                    k.customTangents = 0;
                }
                changed = true;
            }
            ImGui::TextDisabled("此处调整路径与姿态连接；只调快慢请使用整体运镜曲线。");
        }
        if (ImGui::CollapsingHeader("整体运镜 · 节奏")) {
            // 節奏の編集入口を一つにし、端点保護を迂回する数値フィールドを置かない。
            if (ImGui::Button("编辑节奏曲线")) {
                curveView_.activeChannel = "timing";
                curveView_.fitted = false;
                curveMode_ = true;
            }
            if (!shot->camera.timing.keys.empty() && ImGui::Button("恢复原始节奏")) {
                shot->camera.timing = {};
                curveView_.editing.selection.clear();
                curveView_.fitted = false;
                changed = true;
            }
            ImGui::TextWrapped(
                "数值图显示运镜进度，速度图显示进度变化速度；调节奏保持起终机位。框选关键帧后按 F9 可应用缓动。");
        }
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.followAim").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= binding(TEXT::Localize("cine.anchor").c_str(), shot->camera.anchor);
            changed |= binding(TEXT::Localize("cine.lookAt").c_str(), shot->camera.lookAt);
            if (shot->camera.lookAt.IsValid()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(TEXT::Localize("cine.aimWeight").c_str());
                ImGui::SameLine();
                ImGui::PushID("aimWeight");
                changed |= DrawCurve(shot->camera.aimWeight, ShotSourceTime(*shot), "aimWeight");
                ImGui::PopID();
                changed |= ImGui::DragFloat3(fieldLabel(TEXT::Localize("cine.lookOffset").c_str()).c_str(),
                                             &shot->camera.targetOffset.x, 0.02f);
            } else {
                changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.pivotDistance").c_str()).c_str(),
                                            &shot->camera.pivotDistance, 0.05f, 0.1f, 1000.0f, "%.2f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TEXT::Localize("cine.pivotDistanceTip").c_str());
            }
        }
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.rail").c_str())) {
            auto& rail = shot->camera.rail;
            ImGui::TextWrapped("%s", TEXT::Localize("cine.railHint").c_str());
            if (rail.points.empty()) {
                // 姿勢键の位置を通る軌道にする（键が無ければ今の位置から始める）。
                if (IconTextButton(EditorGlyph::Animation, TEXT::Localize("cine.railFromKeys").c_str(),
                                   "railFromKeys")) {
                    ShotCamera resolved;
                    if (!ResolveSequenceShotCamera(sequence, previewBindings_, *shot, scene.GetWorld(),
                                                   scene.GetCameraDirector(), resolved))
                        return changed;
                    for (const auto& poseKey : resolved.keys)
                        rail.points.push_back(poseKey.position);
                    if (rail.points.empty())
                        rail.points.push_back(resolved.base.position);
                    shot->camera.railT = ScalarCurve{0.0f};
                    if (rail.points.size() >= 2) {
                        SetScalarCurveKey(shot->camera.railT, 0.0f, 0.0f);
                        SetScalarCurveKey(shot->camera.railT, shot->sourceOffsetSeconds + shot->durationSeconds, 1.0f);
                    }
                    selectedRailPoint_ = 0;
                    changed = true;
                }
            } else {
                int remove = -1;
                for (size_t i = 0; i < rail.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    char label[32];
                    std::snprintf(label, sizeof(label), "%d", static_cast<int>(i + 1));
                    if (ImGui::RadioButton(label, selectedRailPoint_ == static_cast<int>(i)))
                        selectedRailPoint_ = static_cast<int>(i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth((std::max)(80.0f, ImGui::GetContentRegionAvail().x - 34));
                    changed |= ImGui::DragFloat3("##point", &rail.points[i].x, 0.02f, 0.0f, 0.0f, "%.2f");
                    ImGui::SameLine();
                    if (IconButton(EditorGlyph::Delete, "removePoint", EditorButtonTone::Quiet, {24, 24},
                                   TEXT::Localize("cine.removeRailPoint").c_str()))
                        remove = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (remove >= 0) {
                    rail.points.erase(rail.points.begin() + remove);
                    selectedRailPoint_ = -1;
                    changed = true;
                }
                // 新しい点は空間视图の視点（無ければ最後の点の少し先）に置く。
                if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.addRailPoint").c_str(), "addRailPoint")) {
                    const Camera3D view = directorViewPanel_.GetViewCamera();
                    rail.points.push_back(spaceView_ ? view.GetPosition()
                                                     : rail.points.back() + MATH::Vec3{1.0f, 0.0f, 0.0f});
                    selectedRailPoint_ = static_cast<int>(rail.points.size() - 1);
                    if (rail.points.size() == 2 && shot->camera.railT.keys.empty()) {
                        SetScalarCurveKey(shot->camera.railT, 0.0f, 0.0f);
                        SetScalarCurveKey(shot->camera.railT, shot->sourceOffsetSeconds + shot->durationSeconds, 1.0f);
                    }
                    changed = true;
                }
                ImGui::SameLine();
                if (IconTextButton(EditorGlyph::Delete, TEXT::Localize("cine.clearRail").c_str(), "clearRail")) {
                    rail = {};
                    shot->camera.railT = ScalarCurve{0.0f};
                    selectedRailPoint_ = -1;
                    changed = true;
                }
                changed |= ImGui::Checkbox(TEXT::Localize("cine.railLoop").c_str(), &rail.loop);
                if (rail.points.size() >= 2) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(TEXT::Localize("cine.railProgress").c_str());
                    ImGui::SameLine();
                    ImGui::PushID("railT");
                    changed |= DrawCurve(shot->camera.railT, ShotSourceTime(*shot), "railT");
                    ImGui::PopID();
                }
            }
        }
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.motionChain").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawCameraMotionStack(
                shot->camera.motion, ShotSourceTime(*shot),
                [this](auto& parameter, float time, const auto& channel) {
                    return DrawCurve(parameter, time, channel);
                },
                false);
        }
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.shotFx").c_str()))
            changed |= DrawEffects(shot->effects, ShotLocalTime(*shot) + shot->sourceOffsetSeconds);
        if (ImGui::CollapsingHeader(TEXT::Localize("cine.transition").c_str())) {
            bool blend = shot->transition.mode == CameraCutTransitionMode::EaseInOut;
            if (ImGui::Checkbox(TEXT::Localize("cine.blendMotion").c_str(), &blend)) {
                shot->transition.mode = blend ? CameraCutTransitionMode::EaseInOut : CameraCutTransitionMode::Cut;
                changed = true;
            }
            if (blend)
                changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.blendDuration").c_str()).c_str(),
                                            &shot->transition.durationSeconds, 0.02f, 0, shot->durationSeconds);
            ImGui::TextDisabled("%s", TEXT::Localize("cine.fadeHint").c_str());
        }
    } else if (selectionKind_ == SelectionKind::Cue) {
        for (auto& cue : sequence.presentationCueTrack.clips)
            if (cue.id == selectedId_) {
                PanelTitle(cue.kind == PresentationCueKind::Vfx ? TEXT::Localize("cine.vfxClip").c_str()
                                                                : TEXT::Localize("cine.soundClip").c_str());
                changed |= text(TEXT::Localize("ui.input.name").c_str(), cue.name);
                changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("ui.vfx.text.Start").c_str()).c_str(),
                                            &cue.startSeconds, 0.02f, 0, 10000);
                changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.duration").c_str()).c_str(),
                                            &cue.durationSeconds, 0.02f, 0.01f, 10000);
                changed |= binding(TEXT::Localize("cine.boundObject").c_str(), cue.targetBinding);
                if (cue.kind == PresentationCueKind::Vfx) {
                    AssetFieldOptions options;
                    options.label = TEXT::Localize("menu.workspace.vfx");
                    options.requiredType = AssetType::VfxEffect;
                    changed |= DrawAssetField(&scene.GetAssetDatabase(), options, cue.vfxAsset.value);
                } else {
                    changed |= text(TEXT::Localize("cine.audioFile").c_str(), cue.soundPath);
                    changed |= ImGui::SliderFloat(TEXT::Localize("cine.volume").c_str(), &cue.audio.volume, 0, 1);
                }
                if (ImGui::Button(TEXT::Localize("cine.deleteClip").c_str()))
                    return DeleteSelectedClip(sequence);
                break;
            }
    } else if (selectionKind_ == SelectionKind::Effect) {
        PanelTitle(TEXT::Localize("cine.sequenceFx").c_str(), TEXT::Localize("cine.outputOnly").c_str());
        changed |= DrawEffects(sequence.screenEffects, cursor_.GetTimeSeconds());
    } else {
        PanelTitle(TEXT::Localize("cine.sequence").c_str());
        changed |= text(TEXT::Localize("ui.input.name").c_str(), sequence.name);
        changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("cine.duration").c_str()).c_str(),
                                    &sequence.durationSeconds, 0.1f, 1, 10000, "%.2f s");
        ImGui::TextWrapped("%s", TEXT::Localize("cine.editHint").c_str());
    }
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
