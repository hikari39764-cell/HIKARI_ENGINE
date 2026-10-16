#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

using namespace DIRECTOR_VIEW;

DirectorViewPanelResult DirectorViewPanel::DrawContents(const DocumentSceneBase& scene, EditorViewInstance& view,
                                                        SceneObjectId selectedObjectId,
                                                        const EditorTransformGizmoState& gizmoState, float deltaTime,
                                                        bool runtimePlayActive, bool sceneCameraControls) {
    DirectorViewPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
    bool toolbarCameraChanged = false;
    if (mode_ != DirectorViewMode::Free &&
        (!IsEnabledCamera(scene, targetCameraObjectId_) ||
         (mode_ == DirectorViewMode::Pilot && HasDocumentParent(scene, targetCameraObjectId_)))) {
        SetMode(DirectorViewMode::Free);
    }
    if (runtimePlayActive && mode_ == DirectorViewMode::Pilot) {
        SetMode(DirectorViewMode::Free);
    }

    SceneObjectId toolbarTarget = IsEnabledCamera(scene, selectedObjectId) ? selectedObjectId : targetCameraObjectId_;
    const GameObject* selectedObject = FindRuntimeObject(scene, selectedObjectId);
    const bool selectedObjectIsCamera =
        selectedObject != nullptr && selectedObject->GetComponent<CameraComponent>() != nullptr;
    const bool targetEnabled = IsEnabledCamera(scene, toolbarTarget);
    const bool targetHasParent = targetEnabled && HasDocumentParent(scene, toolbarTarget);

    const auto label = [](const char* key) { return TEXT::Localize(key); };
    const auto icon = [&](EditorGlyph glyph, const char* key) {
        return IconButton(glyph, key, EditorButtonTone::Quiet, {26, 26}, label(key).c_str());
    };
    if (sceneCameraControls) {
        if (icon(EditorGlyph::WorldSpace, "cine.freeView"))
            SetMode(DirectorViewMode::Free);
        ImGui::SameLine();
        ImGui::BeginDisabled(!targetEnabled);
        if (icon(EditorGlyph::Camera, "cine.lookThrough")) {
            SetTargetCamera(toolbarTarget);
            SetMode(DirectorViewMode::LookThrough);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!targetEnabled || targetHasParent || runtimePlayActive);
        if (icon(EditorGlyph::Translate, "cine.pilot")) {
            Camera3D camera;
            if (scene.TryResolveCameraObjectView(toolbarTarget, (std::max)(view.extent.Aspect(), 0.05f), camera)) {
                SetTargetCamera(toolbarTarget);
                pilotCamera_.ResetFromCamera(camera);
                SetMode(DirectorViewMode::Pilot);
            }
        }
        ImGui::SameLine();
        if (icon(EditorGlyph::Focus, "cine.alignCamera")) {
            result.alignCameraRequested = true;
            result.alignCameraObjectId = toolbarTarget;
            result.alignPose = MakeDirectorCameraPose(currentViewCamera_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (icon(EditorGlyph::Settings, "cine.viewOptions"))
        ImGui::OpenPopup("viewOptions");
    ImGui::SameLine();
    DrawEditorViewGuidesButton("guides", view.visualization.guides);
    ImGui::SameLine();
    if (icon(EditorGlyph::Help, "cine.navigationHelp"))
        ImGui::OpenPopup("viewHelp");
    if (ImGui::BeginPopup("viewHelp")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
        ImGui::TextUnformatted(label("cine.navigationHelpText").c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("viewOptions")) {
        if (selectedObject && ImGui::MenuItem(label("cine.frameSelected").c_str())) {
            FocusPoint(ExtractPosition(selectedObject->GetTransform().GetWorldMatrix()), 5);
            toolbarCameraChanged = true;
        }
        const char* presetKeys[]{"ui.vfx.text.Perspective", "ui.vfx.text.Top", "ui.vfx.text.Front",
                                 "ui.input.directionRight"};
        for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(label(presetKeys[i]).c_str())) {
                SetMode(DirectorViewMode::Free);
                freeCamera_.ApplyViewPreset(static_cast<EditorDirectorCameraViewPreset>(i));
                toolbarCameraChanged = true;
            }
        ImGui::Separator();
        const char* shadingKeys[]{"cine.lit", "cine.neutral", "cine.unlit"};
        const auto shadingLabel = label(shadingKeys[static_cast<int>(view.visualization.shadingMode)]);
        if (ImGui::BeginCombo(label("ui.debugView.shading").c_str(), shadingLabel.c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(label(shadingKeys[i]).c_str(),
                                      static_cast<int>(view.visualization.shadingMode) == i))
                    view.visualization.shadingMode = static_cast<EditorViewShadingMode>(i);
            ImGui::EndCombo();
        }
        ImGui::SliderFloat(label("cine.exposure").c_str(), &view.visualization.displayExposure, 0.25f, 4, "%.2fx");
        ImGui::Checkbox(label("cine.cameraOverlays").c_str(), &view.visualization.showCameraOverlays);
        ImGui::Checkbox(label("cine.selectedOnly").c_str(), &view.visualization.showOnlySelectedCamera);
        ImGui::SliderFloat(label("cine.cameraOverlayScale").c_str(), &view.visualization.cameraOverlayScale, 0.5f, 2,
                           "%.1fx");
        float farClip = freeCamera_.GetCamera().GetFarZ();
        if (ImGui::DragFloat(label("cine.farClip").c_str(), &farClip, 10, 10, 100000, "%.0f")) {
            freeCamera_.SetFarZ(farClip);
            toolbarCameraChanged = true;
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = (std::max)(canvasSize.x, kMinimumCanvasSize);
    canvasSize.y = (std::max)(canvasSize.y, kMinimumCanvasSize);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = canvasSize;
    result.viewport = {canvasOrigin.x, canvasOrigin.y, canvasSize.x, canvasSize.y};
    const ImVec2 imageOrigin = canvasOrigin;
    ImGui::GetWindowDrawList()->AddRectFilled(
        canvasOrigin, {canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y}, IM_COL32(12, 16, 22, 255));
    ImGui::SetCursorScreenPos(imageOrigin);
    view.extent.width = ExtentDimension(imageSize.x);
    view.extent.height = ExtentDimension(imageSize.y);
    const float aspect = (std::max)(view.extent.Aspect(), 0.05f);

    const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output = RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
    const uint64_t currentSceneRevision = scene.GetSceneDocumentRevision();
    const bool outputReady = output.ready && output.colorSrv.ptr != 0 && output.width > 0 && output.height > 0 &&
                             output.sceneRevision <= currentSceneRevision;
    if (outputReady) {
        const ImTextureID texture = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(output.colorSrv.ptr));
        ImGui::Image(texture, imageSize);
    } else {
        ImGui::InvisibleButton("##DirectorViewportCanvas", imageSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageOrigin, {imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y},
                                IM_COL32(9, 12, 16, 255));
        const bool outputFailed = output.status == RENDER3D::EDITORVIEW::EditorInteractiveViewStatus::Failed;
        const std::string_view outputMessage =
            outputFailed ? RENDER3D::EDITORVIEW::GetEditorInteractiveViewFailureMessage(output.failure)
                         : std::string_view{"Waiting for Director View output"};
        drawList->AddText({imageOrigin.x + 14.0f, imageOrigin.y + 14.0f},
                          outputFailed ? IM_COL32(255, 122, 104, 255) : IM_COL32(184, 198, 211, 255),
                          outputMessage.data());
    }

    const bool imageHovered = ImGui::IsItemHovered();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImGuiIO& io = ImGui::GetIO();
    const EditorViewInputBlockState inputBlock = QueryEditorViewInputBlockState();
    EditorViewInputSubmission inputSubmission{};
    inputSubmission.rect = {imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y};
    inputSubmission.visible = ImGui::IsItemVisible();
    inputSubmission.focused = windowFocused;
    inputSubmission.hovered = imageHovered;
    inputSubmission.rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    inputSubmission.rightMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    inputSubmission.middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    inputSubmission.middleMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    inputSubmission.leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    inputSubmission.leftMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    inputSubmission.altDown = io.KeyAlt;
    inputSubmission.pointerBlocked = inputBlock.pointer;
    inputSubmission.keyboardBlocked = inputBlock.keyboard;
    // Gizmo capture belongs to the current frame. Clear the previous-frame
    // value before arbitrating a new RMB/MMB/Alt+LMB press.
    inputRouter_.SetGizmoCapture(view.renderViewId, false);
    const EditorViewInputState& inputState = inputRouter_.Submit(view.renderViewId, inputSubmission);

    const bool navigationPressed = !inputBlock.pointer && imageHovered &&
                                   (inputSubmission.rightMouseClicked || inputSubmission.middleMouseClicked ||
                                    (inputSubmission.leftMouseClicked && inputSubmission.altDown));
    if (navigationPressed) {
        ImGui::SetWindowFocus();
        ImGui::ClearActiveID();
    }

    view.interaction.visible = inputState.visible;
    view.interaction.focused = inputState.focused;
    view.interaction.hovered = inputState.hovered;
    view.interaction.mouseCaptured = inputState.IsMouseCaptured();
    view.interaction.keyboardActive = inputState.AcceptsKeyboard();

    if (mode_ == DirectorViewMode::Pilot && !inputBlock.keyboard && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        SetMode(DirectorViewMode::Free);
    }

    EditorDirectorCameraInput cameraInput{};
    const bool allowNavigation = mode_ != DirectorViewMode::LookThrough &&
                                 (!runtimePlayActive || mode_ == DirectorViewMode::Free) && !inputState.gizmoCaptured &&
                                 !inputBlock.pointer;
    if (allowNavigation) {
        cameraInput.lookActive = inputState.rightMouseCaptured;
        cameraInput.orbitActive = inputState.orbitMouseCaptured;
        cameraInput.panActive = inputState.middleMouseCaptured;
        cameraInput.mouseDeltaX = io.MouseDelta.x;
        cameraInput.mouseDeltaY = io.MouseDelta.y;
        cameraInput.wheelDelta = inputState.AcceptsWheel() ? io.MouseWheel : 0.0f;
        cameraInput.fast = io.KeyShift;
        if (inputState.rightMouseCaptured) {
            cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
            cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
            cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
            cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
            cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
            cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
        }
    }

    bool cameraChanged = toolbarCameraChanged;
    if (allowNavigation && inputState.AcceptsKeyboard() && imageHovered && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (selectedObject != nullptr) {
            ActiveController().Focus(ExtractPosition(selectedObject->GetTransform().GetWorldMatrix()));
            cameraChanged = true;
        }
    }

    if (allowNavigation) {
        cameraChanged = ActiveController().Update(cameraInput, deltaTime, aspect) || cameraChanged;
    }

    if (mode_ == DirectorViewMode::LookThrough) {
        if (!scene.TryResolveCameraObjectView(targetCameraObjectId_, aspect, resolvedLookCamera_)) {
            SetMode(DirectorViewMode::Free);
            currentViewCamera_ = freeCamera_.GetCamera();
        } else {
            currentViewCamera_ = resolvedLookCamera_;
        }
    } else {
        currentViewCamera_ = ActiveController().GetCamera();
    }

    result.viewMoved = cameraChanged;
    if (mode_ == DirectorViewMode::Pilot && cameraChanged) {
        result.pilotPoseChanged = true;
        result.pilotCameraObjectId = targetCameraObjectId_;
        result.pilotPose = pilotCamera_.GetPose();
    }

    if (mode_ == DirectorViewMode::Free) {
        view.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
        view.cameraBinding.sceneObjectId = {};
    } else {
        view.cameraBinding.kind = EditorViewCameraSourceKind::SceneCameraObject;
        view.cameraBinding.sceneObjectId = targetCameraObjectId_;
    }

    const bool projectionChanged = std::abs(lastAspect_ - aspect) > 1.0e-5f;
    if (cameraChanged || projectionChanged || cameraCutPending_) {
        ++cameraRevision_;
    }
    lastAspect_ = aspect;

    DrawEditorViewGuides(*ImGui::GetWindowDrawList(), imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y,
                         view.visualization.guides, &currentViewCamera_);

    const bool gizmoAllowed = !runtimePlayActive && !inputBlock.pointer && mode_ != DirectorViewMode::Pilot;
    if (gizmoAllowed && !(selectedObjectIsCamera && gizmoState.operation == EditorTransformGizmoOperation::Scale)) {
        if (selectedObject != nullptr) {
            GameObject proxy{"Director Gizmo Proxy"};
            proxy.SetDocumentId(selectedObjectId);
            Transform3D proxyTransform;
            const bool decomposed =
                MATH::DecomposeTRS(selectedObject->GetTransform().GetWorldMatrix(), proxyTransform.position,
                                   proxyTransform.rotation, proxyTransform.scale);
            if (!decomposed)
                proxyTransform = selectedObject->GetTransform();
            if (selectedObjectIsCamera) {
                proxyTransform.scale = {1.0f, 1.0f, 1.0f};
                proxyTransform.useExplicitMatrix = false;
            }
            (void)proxy.SetLocalTransform(proxyTransform);
            EditorTransformGizmoState directorGizmoState = gizmoState;
            if (io.KeyCtrl) {
                directorGizmoState.snapEnabled = true;
            }
            const EditorTransformGizmoResult gizmoResult =
                transformGizmo_.Draw(proxy, currentViewCamera_, directorGizmoState,
                                     {imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y});
            result.gizmo.interacting = gizmoResult.interacting;
            result.gizmo.changed = gizmoResult.changed;
            result.gizmo.objectId = selectedObjectId;
            result.gizmo.transform = gizmoResult.transform;
            result.gizmo.rotation = gizmoResult.rotation;
            if (gizmoResult.changed && selectedObject->GetParent()) {
                const auto worldMatrix =
                    MATH::Mat4::TRS(gizmoResult.transform.position, gizmoResult.rotation, gizmoResult.transform.scale);
                MATH::Vec3 localPosition, localScale;
                MATH::Quat localRotation;
                if (MATH::DecomposeTRS(MATH::Inverse(selectedObject->GetParent()->GetTransform().GetWorldMatrix()) *
                                           worldMatrix,
                                       localPosition, localRotation, localScale)) {
                    result.gizmo.transform.position = localPosition;
                    result.gizmo.rotation = localRotation;
                    result.gizmo.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(localRotation);
                    result.gizmo.transform.scale = localScale;
                } else
                    result.gizmo.changed = false;
            }
        }
    }
    inputRouter_.SetGizmoCapture(view.renderViewId, result.gizmo.interacting);
    view.interaction.gizmoCaptured = result.gizmo.interacting;

    const bool cameraSelectionClicked = !inputBlock.pointer && imageHovered &&
                                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
                                        !result.gizmo.interacting;
    const SceneObjectId clickedCamera = DrawCameraOverlays(scene, selectedObjectId, currentViewCamera_, imageOrigin,
                                                           imageSize, cameraSelectionClicked, view.visualization);
    if (cameraSelectionClicked && clickedCamera.value != 0) {
        result.selectionRequested = true;
        result.selectedObjectId = clickedCamera;
        SetTargetCamera(clickedCamera);
    }

    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    request.viewId = view.renderViewId;
    request.cameraFrame.camera = currentViewCamera_;
    request.cameraFrame.sourceCameraObjectId = mode_ == DirectorViewMode::Free ? 0 : targetCameraObjectId_.value;
    request.cameraFrame.revision = cameraRevision_;
    request.cameraFrame.cameraCut = cameraCutPending_;
    request.cameraFrame.projectionChanged = projectionChanged;
    request.cameraFrame.valid = true;
    request.width = view.extent.width;
    request.height = view.extent.height;
    request.sceneRevision = scene.GetSceneDocumentRevision();
    request.visible = view.interaction.visible;
    request.continuousRealtime = true;
    request.schedulingPriority = 100u;
    request.shadingMode = ResolveShadingMode(view.visualization.shadingMode);
    request.displayExposure = view.visualization.displayExposure;
    // Camera overlays and the transform gizmo are drawn explicitly by this
    // panel. Replaying every Scene debug helper here creates duplicate light
    // markers and oversized frusta that obscure camera editing.
    request.drawDebug = false;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    cameraCutPending_ = false;

    ImGui::SetCursorScreenPos({canvasOrigin.x, canvasOrigin.y + canvasSize.y});
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    result.mode = mode_;
    result.targetCameraObjectId = targetCameraObjectId_;
#else
    (void)scene;
    (void)view;
    (void)selectedObjectId;
    (void)gizmoState;
    (void)deltaTime;
    (void)runtimePlayActive;
#endif
    return result;
}

} // namespace HIKARI::EDITOR
