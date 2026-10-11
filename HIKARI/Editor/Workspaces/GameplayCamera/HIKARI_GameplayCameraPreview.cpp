#include "Scene/Camera/HIKARI_CameraCollision.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "imgui.h"
#include <algorithm>
#include <numbers>
#include <cmath>

namespace HIKARI::EDITOR {
void GameplayCameraWorkspaceController::DrawViews(DocumentSceneBase& scene, EditorWorkspaceHost& host,
                                                  GameplayCameraWorkspaceResult& workspaceResult) {
    auto& stage = host.GetGameplayCameraObserverView();
    auto& monitor = host.GetGameplayCameraPreviewView();
    auto* cameraObject = scene.GetWorld().FindObject(camera_);
    if (!cameraObject) {
        for (const auto& object : scene.GetWorld().GetObjects())
            if (object && object->GetComponent<CameraComponent>()) {
                cameraObject = object.get();
                SelectCamera(scene, object->GetDocumentId());
                break;
            }
    }
    auto* live = cameraObject ? cameraObject->GetComponent<CameraFollowComponent>() : nullptr;
    if (!target_.value && live)
        target_ = live->GetTargetObjectId();
    if (!lookTarget_.value && live)
        lookTarget_ = live->GameplayBinding().lookTarget;
    const bool runtime = scene.IsRuntimePlayActive();
    // 模拟走动：前の帧に集めた WASD で、前の帧に相机画面に出した向きに対して替身を歩かせてから、相机を評価する。
    if (walk_.Active() && !runtime)
        walk_.Step(scene.GetWorld(), walkInput_, lastMonitorCamera_.GetTarget() - lastMonitorCamera_.GetPosition(),
                   ImGui::GetIO().DeltaTime);
    // 追従の無い机位は、実行時の CameraDirector と同じく物体の位置と向きの画面になる（方案で動かさない）。
    const bool shotCamera = cameraObject != nullptr && live == nullptr;
    if (!runtime && cameraObject && live && target_.value && document_.IsOpen()) {
        const auto binding = std::to_string(camera_.value) + ":" + std::to_string(target_.value) + ":" +
                             std::to_string(lookTarget_.value);
        if (previewBinding_ != binding || sceneRevision_ != scene.GetSceneDocumentRevision()) {
            nlohmann::json values;
            if (live)
                live->Serialize(values);
            // 部件の JSON は物体を参照の形（$hikariRef）で読む。数値を入れると読まれず、追う物が無いままになる。
            values["targetObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(target_.value);
            values["cameraLookTarget"] = SCENE::SERIALIZATION::WriteSceneObjectReference(lookTarget_.value);
            preview_.Deserialize(values);
            previewBinding_ = binding;
            previewRevision_ = 0;
            sceneRevision_ = scene.GetSceneDocumentRevision();
        }
        if (previewRevision_ != document_.Revision()) {
            const auto* previous =
                preview_.GameplayState().source ? preview_.GameplayState().source->FindMode(mode_) : nullptr;
            const auto* current = document_.Profile().FindMode(mode_);
            if (previous && current &&
                (previous->follow.initialYawDegrees != current->follow.initialYawDegrees ||
                 previous->follow.initialPitchDegrees != current->follow.initialPitchDegrees)) {
                preview_.ResetRuntimeState();
                preview_.GameplayState().Reset();
            }
            preview_.GameplayState().previewSource =
                std::make_shared<const CAMERA::GameplayCameraProfile>(document_.Profile());
            previewRevision_ = document_.Revision();
        }
        preview_.GameplayState().requestedMode = mode_;
        if (preview_.Runtime().initialized && (previewYaw_ || previewPitch_ || previewZoom_))
            preview_.SetRuntimeOrbit(preview_.Runtime().yawDegrees + previewYaw_,
                                     preview_.Runtime().pitchDegrees + previewPitch_,
                                     preview_.Runtime().desiredDistance + previewZoom_, preview_.Runtime().distance, 0);
        previewYaw_ = previewPitch_ = previewZoom_ = 0;
        preview_.GameplayState().previewSpeed = previewSpeed_;
        evaluator_.EvaluatePreview(scene.GetWorld(), *cameraObject, preview_, simulate_ ? ImGui::GetIO().DeltaTime : 0);
    }

    if (!runtime && effectsEditing_ && preview_.GameplayState().hasPose) {
        if (const auto* effect = document_.Profile().FindEffect(effect_)) {
            if (effectPlaying_) {
                effectTime_ += std::max(0.0f, ImGui::GetIO().DeltaTime);
                if (effectTime_ >= effect->duration) {
                    if (effectLoop_)
                        effectTime_ = std::fmod(effectTime_, effect->duration);
                    else {
                        effectTime_ = effect->duration;
                        effectPlaying_ = false;
                    }
                }
            }
            if (!effectBypass_ && effectTime_ < effect->duration) {
                auto& pose = preview_.GameplayState().outputPose;
                float fov = pose.verticalFovRadians * 180 / std::numbers::pi_v<float>;
                SEQUENCER::ApplyCameraMotionEffects(effect->effects, effectTime_, pose.eye, pose.target, pose.up, fov,
                                                    previewSpeed_, effectStrength_);
                pose.verticalFovRadians = fov * std::numbers::pi_v<float> / 180;
            }
        }
    }
    const CameraFollowComponent* shown = runtime ? live : &preview_;
    // WASD は観察視口か相机画面に焦点がある時だけ（右ボタンを押している間は観察視口の飛行に使う）。
    walkInput_ = {};
    const auto readWalkInput = [&] {
        if (!walk_.Active() || runtime || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
            ImGui::GetIO().WantTextInput || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;
        walkInput_.forward +=
            (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
        walkInput_.right += (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
        walkInput_.run = walkInput_.run || ImGui::GetIO().KeyShift;
    };
    if (ImGui::Begin(TEXT::LocalizeWindow("gc.observer", "GameplayCamera/Observer").c_str(), nullptr,
                     ImGuiWindowFlags_NoScrollbar)) {
        if (!focused_ && cameraObject) {
            auto minimum = CAMERA::WorldPosition(*cameraObject);
            auto maximum = minimum;
            auto include = [&](const MATH::Vec3& point) {
                minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
                maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
            };
            if (const auto* target = scene.GetWorld().FindObject(target_))
                include(CAMERA::WorldPosition(*target));
            // 初回だけ全体を斜め上から捉え、その後の自由観察を上書きしない。
            const auto center = (minimum + maximum) * .5f;
            const float distance = std::max(8.0f, MATH::Length(maximum - minimum) * 1.2f);
            Camera3D overview;
            overview.SetPerspective(Camera3D::kDefaultFovYRad, 16.0f / 9, .1f, 10000);
            overview.SetLookAt(center + MATH::Normalize(MATH::Vec3{.7f, .65f, -1}) * distance, center);
            observer_.SetFreeCamera(overview);
            observer_.SetTargetCamera(camera_);
            focused_ = true;
        }
        readWalkInput();
        EditorTransformGizmoState gizmo{};
        const auto result =
            observer_.DrawContents(scene, stage, SceneObjectId{}, gizmo, ImGui::GetIO().DeltaTime, runtime, false);
        if (result.selectionRequested) {
            const auto* selected = scene.GetWorld().FindObject(result.selectedObjectId);
            if (selected && selected->GetComponent<CameraComponent>()) {
                SelectCamera(scene, result.selectedObjectId);
            }
        }
        DrawZoneOverlay(scene, result.viewport);
        walk_.DrawOverlay(*ImGui::GetWindowDrawList(), scene.GetWorld(), observer_.GetViewCamera(), result.viewport,
                          walk_.Active() && !runtime ? &lastMonitorCamera_ : nullptr);
        if (shown && !(shotCamera && !runtime) && shown->GameplayState().hasPose) {
            const auto& pose = shown->GameplayState().outputPose;
            ImVec2 eye, target;
            const auto& rect = result.viewport;
            if (DIRECTOR_VIEW::ProjectWorldToViewport(observer_.GetViewCamera(), pose.eye, {rect.x, rect.y},
                                                      {rect.width, rect.height}, eye) &&
                DIRECTOR_VIEW::ProjectWorldToViewport(observer_.GetViewCamera(), pose.target, {rect.x, rect.y},
                                                      {rect.width, rect.height}, target)) {
                auto* draw = ImGui::GetWindowDrawList();
                const auto color =
                    shown->Runtime().collisionLimited ? IM_COL32(255, 155, 85, 255) : IM_COL32(70, 210, 225, 255);
                draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
                draw->AddLine(eye, target, color, 2);
                draw->AddCircle(eye, 8, color, 20, 2);
                draw->AddCircle(target, 4, color, 16, 2);
                draw->PopClipRect();
            }
        }
    } else
        RENDER3D::EDITORVIEW::ClearRequest(stage.renderViewId);
    ImGui::End();
    SERVICES::SetEditorGameViewportSize(0, 0, false);
    bool hasPlayViewport = false;
    if (ImGui::Begin(TEXT::LocalizeWindow("gc.monitor", "GameplayCamera/Monitor").c_str(), nullptr,
                     ImGuiWindowFlags_NoScrollbar)) {
        readWalkInput();
        auto avail = ImGui::GetContentRegionAvail();
        const auto fit = FitEditorViewport((std::max)(avail.x, 1.0f), (std::max)(avail.y, 1.0f), 16.0f / 9);
        auto origin = ImGui::GetCursorScreenPos();
        origin.x += fit.offsetX;
        origin.y += fit.offsetY;
        ImGui::SetCursorScreenPos(origin);
        monitor.extent = {static_cast<uint32_t>(fit.width), static_cast<uint32_t>(fit.height)};
        D3D12_GPU_DESCRIPTOR_HANDLE image{};
        if (runtime) {
            RENDER3D::EDITORVIEW::ClearRequest(monitor.renderViewId);
            SERVICES::SetEditorGameViewportSize(static_cast<int>(fit.width), static_cast<int>(fit.height), true);
            if (POST::PostSystem::IsEditorViewportReady())
                image = POST::PostSystem::GetEditorViewportSrv();
            SetGameViewportInputRect(origin.x, origin.y, fit.width, fit.height, ImGui::IsWindowFocused());
            SetGameViewportGizmoCapture(false);
            hasPlayViewport = true;
            workspaceResult.playViewport = {static_cast<int32_t>(std::lround(origin.x)),
                                            static_cast<int32_t>(std::lround(origin.y)),
                                            static_cast<int32_t>(std::lround(origin.x + fit.width)),
                                            static_cast<int32_t>(std::lround(origin.y + fit.height))};
        } else if (cameraObject && (shotCamera || preview_.GameplayState().hasPose)) {
            Camera3D camera;
            if (shotCamera)
                (void)scene.TryResolveCameraObjectView(camera_, 16.0f / 9, camera);
            else {
                const auto& pose = preview_.GameplayState().outputPose;
                camera.SetPerspective(pose.verticalFovRadians, 16.0f / 9, pose.nearClip, pose.farClip);
                camera.SetLookAt(pose.eye, pose.target, pose.up);
                CAMERA::ResolveGameplayCameraSafety(scene.GetWorld(), camera_, camera, nullptr, &preview_);
            }
            if (walk_.Active()) {
                camera = walk_.Resolve(camera, ImGui::GetIO().DeltaTime);
                walk_.Observe(scene.GetWorld(), camera, activeZone_);
            }
            RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request;
            request.viewId = monitor.renderViewId;
            request.purpose = RENDER3D::RenderViewPurpose::EditorScene;
            request.width = monitor.extent.width;
            request.height = monitor.extent.height;
            request.cameraFrame.camera = camera;
            request.cameraFrame.valid = true;
            const auto changed = [](const auto& a, const auto& b) { return MATH::Length(a - b) > 1e-5f; };
            if (changed(camera.GetPosition(), lastMonitorCamera_.GetPosition()) ||
                changed(camera.GetTarget(), lastMonitorCamera_.GetTarget()) ||
                changed(camera.GetUp(), lastMonitorCamera_.GetUp()) ||
                camera.GetFovYRad() != lastMonitorCamera_.GetFovYRad() ||
                camera.GetNearZ() != lastMonitorCamera_.GetNearZ() || camera.GetFarZ() != lastMonitorCamera_.GetFarZ())
                ++monitorRevision_;
            lastMonitorCamera_ = camera;
            request.cameraFrame.revision = monitorRevision_;
            request.sceneRevision = scene.GetSceneDocumentRevision();
            request.visible = true;
            request.continuousRealtime = simulate_;
            request.drawDebug = false;
            RENDER3D::EDITORVIEW::SubmitRequest(request);
            const auto output = RENDER3D::EDITORVIEW::GetOutput(monitor.renderViewId);
            if (output.ready)
                image = output.colorSrv;
        } else
            RENDER3D::EDITORVIEW::ClearRequest(monitor.renderViewId);
        if (image.ptr)
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(image.ptr)), {fit.width, fit.height});
        else {
            ImGui::InvisibleButton("CameraPreviewCanvas", {fit.width, fit.height});
            ImGui::GetWindowDrawList()->AddText({origin.x + 12, origin.y + 12}, IM_COL32(170, 185, 195, 255),
                                                TEXT::Localize("gc.bindToPreview").c_str());
        }
        workspaceResult.playViewportClicked =
            runtime && image.ptr && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (!runtime && preview_.Settings().orbitInputEnabled && ImGui::IsItemHovered() &&
            !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                const float sensitivity = preview_.Settings().mouseSensitivityDegreesPerPixel;
                previewYaw_ = ImGui::GetIO().MouseDelta.x * sensitivity;
                previewPitch_ =
                    ImGui::GetIO().MouseDelta.y * sensitivity * (preview_.Settings().invertVerticalLook ? -1.0f : 1.0f);
            }
            previewZoom_ = -ImGui::GetIO().MouseWheel * preview_.Settings().mouseWheelZoomUnitsPerStep;
        }
    } else
        RENDER3D::EDITORVIEW::ClearRequest(monitor.renderViewId);
    ImGui::End();
    if (!hasPlayViewport)
        ClearGameViewportInputRect();
}
} // namespace HIKARI::EDITOR
