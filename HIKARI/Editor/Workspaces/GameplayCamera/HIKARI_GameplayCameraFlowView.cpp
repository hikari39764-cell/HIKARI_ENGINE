#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <numbers>
namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::Localize(key).c_str(); }
} // namespace
void GameplayCameraWorkspaceController::DrawFlowTimeline(DocumentSceneBase& scene, EditorWorkspaceHost& host,
                                                         EditorOperationRegistry& operations) {
    auto* object = scene.GetWorld().FindObject(flowId_);
    const auto* flow = object ? object->GetComponent<CameraFlowComponent>() : nullptr;
    if (!flow) {
        ImGui::TextDisabled("%s", L("cf.start"));
        return;
    }
    const auto settings = flow->Settings();
    const auto runtime = flow->Runtime();
    const auto* camera = scene.GetWorld().FindObject(settings.cameraObjectId);
    const auto* follow = camera ? camera->GetComponent<CameraFollowComponent>() : nullptr;
    const auto* sequence = scene.GetAssetDatabase().FindByGuid(settings.sequence);
    const auto stateKey = std::string("cf.state.") + CameraFlowStageId(runtime.stage);
    ImGui::SameLine();
    StatusBadge(L(stateKey.c_str()),
                runtime.stage == CameraFlowStage::Invalid || runtime.stage == CameraFlowStage::Interrupted
                    ? EditorStatusTone::Warning
                    : EditorStatusTone::Ready);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", object->GetName().c_str());
    const bool playing = scene.IsRuntimePlayActive();
    if (playing) {
        ImGui::SameLine();
        ImGui::BeginDisabled(runtime.stage == CameraFlowStage::Playing || runtime.stage == CameraFlowStage::Returning);
        if (IconButton(EditorGlyph::StepForward, "trigger_flow", EditorButtonTone::Quiet, {26, 26}, L("cf.triggerNow")))
            message_ =
                operations.Execute("camera_flow.control", {{"objectId", flowId_.value}, {"action", "trigger"}}).message;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(runtime.stage != CameraFlowStage::Playing);
        if (IconButton(EditorGlyph::Stop, "interrupt_flow", EditorButtonTone::Quiet, {26, 26}, L("cf.interrupt")))
            message_ = operations.Execute("camera_flow.control", {{"objectId", flowId_.value}, {"action", "interrupt"}})
                           .message;
        ImGui::EndDisabled();
    }
    const int stage = runtime.stage == CameraFlowStage::Playing                                                    ? 1
                      : runtime.stage == CameraFlowStage::Returning || runtime.stage == CameraFlowStage::Completed ? 2
                                                                                                                   : 0;
    const char* titles[] = {L("cf.region"), L("cf.sequence"), L("cf.after")};
    const EditorGlyph glyphs[] = {EditorGlyph::Gizmo, EditorGlyph::Sequence, EditorGlyph::Camera};
    std::string values[] = {settings.triggerOnExit ? L("cf.onExit") : L("cf.onEnter"),
                            sequence ? sequence->displayName : L("cf.direct"),
                            settings.afterMode.empty() ? L("cf.keepMode") : settings.afterMode};
    if (follow && follow->GameplayState().source)
        if (const auto* mode = follow->GameplayState().source->FindMode(settings.afterMode))
            values[2] = mode->name;
    const float width = std::max(90.0f, (ImGui::GetContentRegionAvail().x - 48) / 3);
    for (int i = 0; i < 3; ++i) {
        if (i) {
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        const auto origin = ImGui::GetCursorScreenPos();
        const bool selected = playing && stage == i;
        if (ImGui::InvisibleButton("stage", {width, 62})) {
            if (i == 0) {
                flowEditing_ = true;
                observer_.FocusPoint(CAMERA::WorldPosition(*object), 10);
                focused_ = true;
            } else if (i == 1 && settings.sequence.IsValid()) {
                EditorWorkspaceOpenRequest request;
                request.workspaceId = EditorWorkspaceId::Cinematics;
                request.sequenceAssetGuid = settings.sequence;
                host.RequestOpen(request);
            } else if (i == 2) {
                if (follow && follow->GameplayBinding().profile != document_.Guid() && document_.IsDirty())
                    message_ = L("gc.saveBeforeBinding");
                else if (follow &&
                         (follow->GameplayBinding().profile == document_.Guid() ||
                          document_.Open(scene.GetAssetDatabase(), follow->GameplayBinding().profile, message_))) {
                    flowEditing_ = false;
                    mode_ = settings.afterMode.empty() ? document_.Profile().defaultMode : settings.afterMode;
                }
            }
        }
        auto* draw = ImGui::GetWindowDrawList();
        const auto color = selected ? IM_COL32(60, 204, 218, 255) : IM_COL32(125, 146, 166, 255);
        draw->AddRectFilled(origin, {origin.x + width, origin.y + 62},
                            selected ? IM_COL32(22, 59, 68, 255) : IM_COL32(25, 31, 39, 255), 6);
        draw->AddRect(origin, {origin.x + width, origin.y + 62}, color, 6);
        DrawEditorGlyph(*draw, glyphs[i], {origin.x + 20, origin.y + 20}, 16, color);
        draw->PushClipRect(origin, {origin.x + width - 8, origin.y + 62}, true);
        draw->AddText({origin.x + 37, origin.y + 10}, color, titles[i]);
        draw->AddText({origin.x + 12, origin.y + 36}, IM_COL32(220, 228, 235, 255), values[i].c_str());
        draw->PopClipRect();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", values[i].c_str());
        ImGui::PopID();
    }
    if (runtime.stage == CameraFlowStage::Playing) {
        const float progress = runtime.duration > 0 ? runtime.time / runtime.duration : 0;
        char label[96];
        std::snprintf(label, sizeof(label), "%.2f / %.2f s", runtime.time, runtime.duration);
        ImGui::ProgressBar(progress, {-1, 14}, label);
    }
    if (!runtime.diagnostic.empty())
        ImGui::TextWrapped("%s", runtime.diagnostic.c_str());
    else
        ImGui::TextDisabled("%s", L("cf.timelineHint"));
}
void GameplayCameraWorkspaceController::DrawFlowOverlay(DocumentSceneBase& scene, const EditorViewportRect& rect) {
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    auto project = [&](const MATH::Vec3& point, ImVec2& result) {
        return DIRECTOR_VIEW::ProjectWorldToViewport(observer_.GetViewCamera(), point, {rect.x, rect.y},
                                                     {rect.width, rect.height}, result);
    };
    auto line = [&](const MATH::Vec3& a, const MATH::Vec3& b, ImU32 color, float width = 1.5f) {
        ImVec2 p, q;
        if (project(a, p) && project(b, q))
            draw->AddLine(p, q, color, width);
    };
    for (const auto& object : scene.GetWorld().GetObjects()) {
        if (!object)
            continue;
        const auto* flow = object->GetComponent<CameraFlowComponent>();
        if (!flow)
            continue;
        CameraFlowComponent draft;
        const auto* shown = flow;
        MATH::Mat4 matrix = object->GetTransform().GetWorldMatrix();
        // ドラフト形状も見えるが、適用までは実行中のシーンを変更しない。
        if (object->GetDocumentId() == flowId_ && flowDirty_) {
            draft.Deserialize(flowDraft_["settings"]);
            shown = &draft;
            const auto& p = flowDraft_["position"];
            const auto& r = flowDraft_["rotation"];
            constexpr float rad = std::numbers::pi_v<float> / 180;
            matrix = MATH::Mat4::TRS(
                {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()},
                MATH::Quat::FromEulerXYZ(r[0].get<float>() * rad, r[1].get<float>() * rad, r[2].get<float>() * rad),
                object->GetTransform().scale);
            if (object->GetParent())
                matrix = object->GetParent()->GetTransform().GetWorldMatrix() * matrix;
        }
        const auto& size = shown->Settings().halfExtents;
        MATH::Vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            const auto point = matrix.TransformPoint(
                {i & 1 ? size.x : -size.x, i & 2 ? size.y : -size.y, i & 4 ? size.z : -size.z, 1});
            corners[i] = {point.x, point.y, point.z};
        }
        const auto color = flow->Runtime().inside               ? IM_COL32(100, 235, 159, 255)
                           : object->GetDocumentId() == flowId_ ? IM_COL32(65, 203, 219, 255)
                                                                : IM_COL32(118, 139, 158, 150);
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit))
                    line(corners[i], corners[i | bit], color);
        ImVec2 center;
        if (project(CAMERA::WorldPosition(*object), center))
            draw->AddText(center, color, object->GetName().c_str());
    }
    if (flowEditing_ && !scene.IsRuntimePlayActive() && !flowDraft_.empty()) {
        const auto* object = scene.GetWorld().FindObject(flowId_);
        const auto* parent = object ? object->GetParent() : nullptr;
        const auto scale = object ? object->GetTransform().scale : MATH::Vec3{1, 1, 1};
        auto position = JSONREAD::Vec3Or(flowDraft_["position"], {});
        auto rotation = JSONREAD::Vec3Or(flowDraft_["rotation"], {});
        auto extents = JSONREAD::Vec3Or(flowDraft_["settings"]["halfExtents"], {2, 2, 2});
        constexpr float rad = std::numbers::pi_v<float> / 180;
        auto matrix =
            MATH::Mat4::TRS(position, MATH::Quat::FromEulerXYZ(rotation.x * rad, rotation.y * rad, rotation.z * rad),
                            {scale.x * extents.x, scale.y * extents.y, scale.z * extents.z});
        if (parent)
            matrix = parent->GetTransform().GetWorldMatrix() * matrix;
        TransformData transform;
        MATH::Quat q;
        if (MATH::DecomposeTRS(matrix, transform.position, q, transform.scale)) {
            transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(q);
            auto state = flowGizmoState_;
            state.snapEnabled = ImGui::GetIO().KeyCtrl;
            const auto edit = flowGizmo_.DrawTransform(transform, observer_.GetViewCamera(), state, rect);
            if (edit.changed) {
                auto local = MATH::Mat4::TRS(edit.transform.position, edit.rotation, edit.transform.scale);
                if (parent)
                    local = MATH::Inverse(parent->GetTransform().GetWorldMatrix()) * local;
                MATH::Vec3 p, s;
                MATH::Quat r;
                if (MATH::DecomposeTRS(local, p, r, s)) {
                    const auto euler = MATH::EulerXYZDegreesFromQuatNearest(r, rotation);
                    flowDraft_["position"] = {p.x, p.y, p.z};
                    flowDraft_["rotation"] = {euler.x, euler.y, euler.z};
                    if (state.operation == EditorTransformGizmoOperation::Scale)
                        flowDraft_["settings"]["halfExtents"] = {
                            std::max(.01f, std::abs(s.x / std::max(.001f, std::abs(scale.x)))),
                            std::max(.01f, std::abs(s.y / std::max(.001f, std::abs(scale.y)))),
                            std::max(.01f, std::abs(s.z / std::max(.001f, std::abs(scale.z))))};
                    flowDirty_ = true;
                }
            }
        }
    }
    const auto* owner = scene.GetWorld().FindObject(flowId_);
    const auto* selected = owner ? owner->GetComponent<CameraFlowComponent>() : nullptr;
    if (selected) {
        const auto& settings = selected->Settings();
        const auto key = settings.sequence.value + ":" + std::to_string(scene.GetAssetDatabase().GetContentRevision());
        if (flowSequenceKey_ != key) {
            flowSequenceKey_ = key;
            flowSequence_.reset();
            if (const auto* record = scene.GetAssetDatabase().FindByGuid(settings.sequence)) {
                auto asset = std::make_shared<SequenceAsset>();
                std::string error;
                auto path = record->sourcePath.is_absolute()
                                ? record->sourcePath
                                : scene.GetAssetDatabase().GetProjectRoot() / record->sourcePath;
                if (LoadSequenceAsset(path, settings.sequence, *asset, &error))
                    flowSequence_ = asset;
            }
        }
        if (flowSequence_) {
            SEQUENCER::SequenceBindingContext bindings;
            (void)bindings.BindSlot("Player", settings.subjectObjectId);
            (void)bindings.BindSlot("Camera", settings.cameraObjectId);
            (void)bindings.BindSlot("Owner", flowId_);
            const auto& sequence = flowSequence_->sequence;
            const auto samples =
                std::max(2, 96 / static_cast<int>(std::max(size_t{1}, sequence.cameraCutTrack.clips.size())));
            for (const auto& shot : sequence.cameraCutTrack.clips) {
                MATH::Vec3 previous{};
                bool valid = false;
                for (int i = 0; i <= samples; ++i) {
                    Camera3D camera;
                    CinematicCameraEvaluation evaluation;
                    const float time =
                        shot.startTimeSeconds + shot.durationSeconds * (static_cast<float>(i) / samples) * .9999f;
                    if (!SEQUENCER::EvaluateSequenceCameraView(sequence, bindings, scene.GetWorld(),
                                                               scene.GetCameraDirector(), time, 16.f / 9, evaluation,
                                                               camera)) {
                        valid = false;
                        continue;
                    }
                    auto point = camera.GetPosition();
                    if (valid)
                        line(previous, point, IM_COL32(231, 181, 104, 220));
                    previous = point;
                    valid = true;
                    if (i == 0) {
                        ImVec2 screen;
                        if (project(point, screen)) {
                            draw->AddCircle(screen, 5, IM_COL32(231, 181, 104, 255));
                            draw->AddText({screen.x + 8, screen.y}, IM_COL32(231, 181, 104, 255),
                                          shot.camera.name.c_str());
                        }
                    }
                }
            }
        }
    }
    if (scene.IsRuntimePlayActive()) {
        const auto& frame = scene.GetCameraDirector().GetResolvedFrame();
        if (frame.valid) {
            const auto eye = frame.camera.GetPosition();
            const auto target = frame.camera.GetTarget();
            line(eye, target, IM_COL32(255, 213, 100, 255), 3);
            ImVec2 point;
            if (project(eye, point)) {
                draw->AddCircle(point, 10, IM_COL32(255, 213, 100, 255), 24, 2);
                draw->AddText({point.x + 14, point.y}, IM_COL32(255, 213, 100, 255), L("cf.liveCamera"));
            }
        }
    }
    draw->PopClipRect();
}
} // namespace HIKARI::EDITOR
