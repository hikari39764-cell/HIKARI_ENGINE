#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewGuides.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "imgui.h"

namespace HIKARI {

using EDITOR::HandleTransformGizmoShortcuts;
using EDITOR::DOCUMENT_SCENE::CollectObjectOverlayIcons;
using EDITOR::DOCUMENT_SCENE::DrawObjectOverlayIcons;
using EDITOR::DOCUMENT_SCENE::DrawRenderDebugViewCombo;
using EDITOR::DOCUMENT_SCENE::DrawViewportDebugOptions;
using EDITOR::DOCUMENT_SCENE::ObjectIconTargets;

void DocumentSceneEditorController::DrawGameViewportWindow(DocumentSceneBase& scene,
                                                           EDITOR::EditorPlaySession& playSession,
                                                           std::string_view stableWindowId) {
    viewportTransformHistory_.BeginFrame();
    bool open = context_.windows.viewport.showGameView;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (focusGameViewportAfterPreview_ && workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene)) {
        ImGui::SetNextWindowFocus();
        focusGameViewportAfterPreview_ = false;
    }
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.gameView", stableWindowId).c_str(), &open, flags)) {
        context_.windows.viewport.showGameView = open;
        playSession.ReleaseEmbeddedInput();
        EDITOR::ClearGameViewportInputRect();
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        viewportTransformHistory_.EndFrame(scene.GetSceneDocument());
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }
    context_.windows.viewport.showGameView = open;
    if (!open) {
        playSession.ReleaseEmbeddedInput();
    }

    const bool selectedObjectIsCamera = context_.selection.selectedObject != nullptr &&
                                        context_.selection.selectedObject->GetComponent<CameraComponent>() != nullptr;
    const float toolbarHeight = context_.windows.viewport.showViewportHud ? 38.0f : 0.0f;
    if (toolbarHeight > 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
        if (ImGui::BeginChild("##GameViewToolbar", ImVec2(0.0f, toolbarHeight), false, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SetCursorPosY(8.0f);
            ImGui::Dummy(ImVec2(8.0f, 0.0f));
            ImGui::SameLine();
            const bool previewRunning = playSession.IsRunning();
            if (EDITOR::IconButton(
                    previewRunning ? EDITOR::EditorGlyph::Stop : EDITOR::EditorGlyph::Play, "EmbeddedPlay",
                    previewRunning ? EDITOR::EditorButtonTone::Danger : EDITOR::EditorButtonTone::Primary,
                    ImVec2(26.0f, 26.0f),
                    previewRunning ? TEXT::Localize("ui.viewport.stopPlay").c_str()
                                   : TEXT::Localize("ui.viewport.playInGameView").c_str())) {
                ToggleGamePreview(scene, playSession);
            }
            ImGui::SameLine(0.0f, 3.0f);
            if (ImGui::ArrowButton("##PlayModeMenu", ImGuiDir_Down)) {
                ImGui::OpenPopup("Play Mode Menu");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TEXT::Localize("ui.viewport.playOptions").c_str());
            }
            if (ImGui::BeginPopup("Play Mode Menu")) {
                if (ImGui::MenuItem(TEXT::Localize("ui.viewport.playInNewWindow").c_str(), nullptr, false,
                                    !previewRunning)) {
                    LaunchWindowedGamePreview(scene, playSession);
                }
                ImGui::EndPopup();
            }
            if (playSession.IsEmbeddedRunning()) {
                ImGui::SameLine();
                if (EDITOR::IconButton(playSession.IsPaused() ? EDITOR::EditorGlyph::Play : EDITOR::EditorGlyph::Pause,
                                       "EmbeddedPlayPause",
                                       playSession.IsPaused() ? EDITOR::EditorButtonTone::Primary
                                                              : EDITOR::EditorButtonTone::Neutral,
                                       ImVec2(26.0f, 26.0f),
                                       playSession.IsPaused() ? TEXT::Localize("ui.viewport.resumePlay").c_str()
                                                              : TEXT::Localize("ui.viewport.pausePlay").c_str())) {
                    playSession.TogglePause();
                }
            }
            if (playSession.GetState() != EDITOR::EditorPlayState::Stopped) {
                ImGui::SameLine();
                EDITOR::StatusText(playSession.IsPaused()
                                       ? TEXT::Localize("ui.viewport.paused").c_str()
                                       : (previewRunning ? TEXT::Localize("ui.viewport.playRunning").c_str()
                                                         : TEXT::Localize("ui.viewport.playStatus").c_str()),
                                   previewRunning ? EDITOR::EditorStatusTone::Ready
                                                  : (playSession.GetState() == EDITOR::EditorPlayState::Failed
                                                         ? EDITOR::EditorStatusTone::Error
                                                         : EDITOR::EditorStatusTone::Normal));
                if (ImGui::IsItemHovered() && !playSession.GetStatusMessage().empty()) {
                    ImGui::SetTooltip("%s", playSession.GetStatusMessage().c_str());
                }
            }
            if (!previewRunning) {
                ImGui::SameLine(0.0f, 8.0f);
                const EDITOR::ViewportAuthoringToolbarResult authoringToolbar =
                    EDITOR::DrawViewportAuthoringToolbar(context_, selectedObjectIsCamera);
                DrawCameraPilotButtons(scene);
                ImGui::SameLine();
                EDITOR::DrawEditorViewGuidesButton("sceneGuides", sceneViewGuides_);
                // 美術向けの見え方の切り替え：素模（材質を灰色にして光と形だけ）と明暗（白黒）。
                ImGui::SameLine();
                const bool clay = context_.viewportDebug.renderView == RenderDebugView::NeutralClay;
                if (EDITOR::ToggleButton(TEXT::Localize("ui.viewport.clay").c_str(), "##clayView", clay,
                                         ImVec2(0.0f, 0.0f), TEXT::Localize("ui.viewport.clayHint").c_str())) {
                    context_.viewportDebug.renderView = clay ? RenderDebugView::None : RenderDebugView::NeutralClay;
                }
                ImGui::SameLine();
                if (EDITOR::ToggleButton(TEXT::Localize("ui.viewport.valueView").c_str(), "##valueView",
                                         context_.overlays.valueView, ImVec2(0.0f, 0.0f),
                                         TEXT::Localize("ui.viewport.valueViewHint").c_str())) {
                    context_.overlays.valueView = !context_.overlays.valueView;
                }
                // 露出の固定：今の露出で止める（補正を足し戻した EV100 を手動露出として使う）。
                ImGui::SameLine();
                const bool exposureLocked = context_.overlays.lockedExposureEv100.has_value();
                if (EDITOR::ToggleButton(TEXT::Localize("ui.viewport.lockExposure").c_str(), "##lockExposure",
                                         exposureLocked, ImVec2(0.0f, 0.0f),
                                         TEXT::Localize("ui.viewport.lockExposureHint").c_str())) {
                    if (exposureLocked) {
                        context_.overlays.lockedExposureEv100.reset();
                    } else {
                        context_.overlays.lockedExposureEv100 =
                            scene.GetCurrentExposureEv100() + scene.GetSceneEnvironment().exposure.compensationEv;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("EV %.1f", scene.GetCurrentExposureEv100());
                // A/B 分屏（布景）：今の画を A として左に残し、預設や灯を変えた右の画と見比べる。
                if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::LookDev)) {
                    ImGui::SameLine();
                    bool& split = lookDevWorkspace_.SplitCompare();
                    if (EDITOR::ToggleButton(TEXT::Localize("ui.lookDev.splitCompare").c_str(), "##splitCompare", split,
                                             ImVec2(0.0f, 0.0f),
                                             TEXT::Localize("ui.lookDev.splitCompareHint").c_str())) {
                        split = !split;
                        if (split) {
                            lookDevWorkspace_.SplitCamera() = scene.GetCamera().GetViewProj();
                            POST::PostSystem::RequestEditorSnapshot();
                        } else {
                            POST::PostSystem::ClearEditorSnapshot();
                        }
                    }
                }
                if (authoringToolbar.settingsRequested) {
                    ImGui::OpenPopup("ViewportToolSettingsPopup");
                }
                if (ImGui::BeginPopup("ViewportToolSettingsPopup")) {
                    ImGui::SeparatorText(TEXT::Localize("ui.viewport.settingsViewport").c_str());
                    DrawViewportDebugOptions(context_.overlays, context_.viewportPerformance);
                    ImGui::SeparatorText(TEXT::Localize("ui.viewport.settingsGizmos").c_str());
                    ImGui::Checkbox(TEXT::Localize("ui.viewport.onlySelectedObject").c_str(),
                                    &context_.gizmos.showOnlySelectedObject);
                    const ComponentGizmoRegistry& gizmoRegistry = scene.GetComponentGizmoRegistry();
                    for (const ComponentGizmoProvider& provider : gizmoRegistry.GetProviders()) {
                        bool visible = context_.gizmos.IsProviderVisible(provider.providerId, provider.defaultVisible);
                        if (ImGui::Checkbox(provider.displayName.c_str(), &visible)) {
                            context_.gizmos.SetProviderVisible(provider.providerId, visible);
                        }
                    }
                    ImGui::SeparatorText(TEXT::Localize("ui.viewport.settingsSnap").c_str());
                    ImGui::Checkbox(TEXT::Localize("ui.viewport.groundSnap").c_str(),
                                    &context_.transformGizmo.groundSnap);
                    ImGui::DragFloat3(TEXT::Localize("ui.viewport.translate").c_str(),
                                      &context_.transformGizmo.translateSnap.x, 0.05f, 0.001f, 100.0f);
                    ImGui::DragFloat(TEXT::Localize("ui.viewport.rotate").c_str(),
                                     &context_.transformGizmo.rotateSnapDeg, 0.5f, 0.1f, 180.0f,
                                     TEXT::Localize("ui.viewport.degFormat").c_str());
                    ImGui::DragFloat(TEXT::Localize("ui.viewport.scale").c_str(), &context_.transformGizmo.scaleSnap,
                                     0.01f, 0.001f, 10.0f);
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                RENDER3D::RenderQualitySettings qualitySettings = RENDER3D::GetRenderQualitySettings();
                EDITOR::ToolbarLabel(TEXT::Localize("ui.viewport.render").c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(118.0f);
                if (ImGui::BeginCombo("##GameViewRenderResolution",
                                      RENDER3D::RenderResolutionPresetLabel(qualitySettings.sceneResolution),
                                      ImGuiComboFlags_NoArrowButton)) {
                    constexpr RENDER3D::RenderResolutionPreset presets[] = {
                        RENDER3D::RenderResolutionPreset::Viewport, RENDER3D::RenderResolutionPreset::P720,
                        RENDER3D::RenderResolutionPreset::P1080,    RENDER3D::RenderResolutionPreset::P1440,
                        RENDER3D::RenderResolutionPreset::P2160,
                    };
                    for (RENDER3D::RenderResolutionPreset preset : presets) {
                        const bool selected = qualitySettings.sceneResolution == preset;
                        if (ImGui::Selectable(RENDER3D::RenderResolutionPresetLabel(preset), selected)) {
                            qualitySettings.sceneResolution = preset;
                            RENDER3D::SetRenderQualitySettings(qualitySettings);
                            renderQualitySavePending_ = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                EDITOR::ToolbarLabel(TEXT::Localize("ui.viewport.view").c_str());
                ImGui::SameLine();
                DrawRenderDebugViewCombo("##GameViewDebug", context_.viewportDebug, 144.0f);
                if (context_.viewportDebug.renderView != RenderDebugView::None) {
                    ImGui::SameLine();
                    EDITOR::StatusText(TEXT::Localize("ui.viewport.diagnosticOutput").c_str(),
                                       EDITOR::EditorStatusTone::Ready);
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImVec2 imageSize = ImGui::GetContentRegionAvail();
    imageSize.x = (std::max)(imageSize.x, 1.0f);
    imageSize.y = (std::max)(imageSize.y, 1.0f);

    const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    const bool gameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // 布景で置く物を選んでいる間は、クリックを選択ではなく配置に使い、R / Esc は配置の操作にする。
    const bool lookDevActive = workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::LookDev);
    // 単独表示と A/B 分屏は布景の中だけ。他の工作区では灯が消えたままにならないよう解く。
    if (!lookDevActive) {
        context_.overlays.soloLightObjectId = 0u;
        if (lookDevWorkspace_.SplitCompare()) {
            lookDevWorkspace_.SplitCompare() = false;
            POST::PostSystem::ClearEditorSnapshot();
        }
    }
    const bool lookDevPlacing = lookDevActive && lookDevWorkspace_.ArmedItem().has_value();
    auto processViewportShortcuts = [&]() {
        if (playSession.IsRunning()) {
            return;
        }
        const bool viewportShortcutFocused = gameViewFocused;
        if (!lookDevPlacing) {
            HandleTransformGizmoShortcuts(context_.transformGizmo, viewportShortcutFocused);
        }
        const bool standardCommandExecuted = commandRouter_.ProcessViewportShortcuts(viewportShortcutFocused);
        if (!standardCommandExecuted &&
            EDITOR::CanUseEditorShortcut(EDITOR::EditorShortcutScope::Viewport, viewportShortcutFocused) &&
            context_.selection.selectedObject != nullptr && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            sceneInspectorPanel_.RequestRename(*context_.selection.selectedObject);
        }
    };
    EDITOR::SetGameViewportInputRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y, gameViewFocused);
    SERVICES::SetEditorGameViewportSize(static_cast<int>(imageSize.x + 0.5f), static_cast<int>(imageSize.y + 0.5f),
                                        true);
    UpdateCameraPilot(scene, playSession.IsRunning());
    auto drawTransformGizmoOverlay = [&]() -> bool {
        bool gizmoCapture = false;
        const EDITOR::EditorViewportRect viewportRect{imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y};
        // 操縦中の機位は視点そのものなので手柄を出さない。
        if (!ImGui::IsPopupOpen("SceneViewportContextMenu") && context_.selection.selectedObject != nullptr &&
            !cameraPilot_.Active() &&
            !EDITOR::IsObjectEditorLocked(scene.GetSceneDocument(), context_.selection.GetActiveObjectId())) {
            EditorTransformGizmoState gizmoState = context_.transformGizmo;
            if (ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
                gizmoState.snapEnabled = true;
            }
            const EDITOR::SceneSelectionTransformResult transformResult = EDITOR::DrawSceneSelectionTransformGizmo(
                scene, context_.selection, transformGizmo_, gizmoState, viewportRect);
            gizmoCapture = transformResult.gizmo.interacting;

            const bool multipleObjects = context_.selection.GetSelectedObjectCount() > 1u;
            std::string historyLabel =
                TEXT::Localize(multipleObjects ? "ui.history.moveObjects" : "ui.history.moveObject");
            switch (gizmoState.operation) {
            case EditorTransformGizmoOperation::Rotate:
                historyLabel = TEXT::Localize(multipleObjects ? "ui.history.rotateObjects" : "ui.history.rotateObject");
                break;
            case EditorTransformGizmoOperation::Scale:
                historyLabel = TEXT::Localize(multipleObjects ? "ui.history.scaleObjects" : "ui.history.scaleObject");
                break;
            case EditorTransformGizmoOperation::Translate:
            default: break;
            }
            if (!transformResult.historyLabelOverride.empty()) {
                historyLabel = transformResult.historyLabelOverride;
            }
            viewportTransformHistory_.ObserveBeforeApply(
                scene.GetSceneDocument(), context_.selection.selectedObject->GetDocumentId(), historyLabel.c_str(),
                transformResult.gizmo.manipulating || transformResult.gizmo.changed,
                context_.sceneDirty || scene.HasUnsavedSceneChanges());

            if (!transformResult.changedObjectIds.empty()) {
                EDITOR::CommitSceneSelectionTransforms(scene, transformResult);
                context_.sceneDirty = true;
                scene.SetUnsavedSceneChanges(true);
                viewportTransformHistory_.MarkChanged();
            }
        }

        EDITOR::SetGameViewportGizmoCapture(gizmoCapture);
        return gizmoCapture;
    };

    const EDITOR::SceneViewportRect sceneViewportRect{imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y};
    bool viewportImageHovered = false;
    bool gizmoCapture = false;
    const bool ready = POST::PostSystem::IsEditorViewportReady();
    const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv = POST::PostSystem::GetEditorViewportSrv();
    if (ready && viewportSrv.ptr != 0) {
        const ImTextureID textureId = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewportSrv.ptr));
        ImGui::Image(textureId, imageSize);
        viewportImageHovered = ImGui::IsItemHovered();
        // A/B 分屏の A（凍結した画）を境目の左へ重ねる。補助線と手柄はこの上に描く。
        if (const D3D12_GPU_DESCRIPTOR_HANDLE snapshot = POST::PostSystem::GetEditorSnapshotSrv();
            lookDevActive && lookDevWorkspace_.SplitCompare() && snapshot.ptr != 0) {
            const float split = lookDevWorkspace_.SplitPosition();
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(snapshot.ptr)), imageOrigin,
                {imageOrigin.x + imageSize.x * split, imageOrigin.y + imageSize.y}, {0.0f, 0.0f}, {split, 1.0f});
        }
        EDITOR::DrawEditorViewGuides(*ImGui::GetWindowDrawList(), imageOrigin.x, imageOrigin.y, imageSize.x,
                                     imageSize.y, sceneViewGuides_, &scene.GetCamera());
        processViewportShortcuts();
        // Drop target は viewport 画像の直後に置き、overlay の項目に奪われないようにする。
        if (!playSession.IsRunning()) {
            HandleGameViewportAssetDrop(scene);
            gizmoCapture = drawTransformGizmoOverlay();
        }
    } else {
        const ImVec2 max{imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageOrigin, max, IM_COL32(8, 10, 13, 255));
        drawList->AddRect(imageOrigin, max, IM_COL32(80, 108, 124, 160), 4.0f, 0, 1.0f);
        drawList->AddText(ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f), IM_COL32(190, 205, 215, 255),
                          TEXT::Localize("ui.viewport.waitingForTexture").c_str());
        ImGui::Dummy(imageSize);
        viewportImageHovered = ImGui::IsItemHovered();
        processViewportShortcuts();
        // Dummy が viewport 全体の当たり判定になるので、overlay を描く前に drop target にする。
        if (!playSession.IsRunning()) {
            HandleGameViewportAssetDrop(scene);
            gizmoCapture = drawTransformGizmoOverlay();
        }
    }

    const INPUT::MouseCaptureRegion embeddedInputRegion{
        static_cast<int32_t>(std::lround(imageOrigin.x)),
        static_cast<int32_t>(std::lround(imageOrigin.y)),
        static_cast<int32_t>(std::lround(imageOrigin.x + imageSize.x)),
        static_cast<int32_t>(std::lround(imageOrigin.y + imageSize.y)),
    };
    // 試玩中の項目の工作区（行動樹など、場景を見ながら別の物を編集する所）では、Alt + クリックで物体を選ぶ
    // （何を観察するかを決める）。このクリックは游戏へ渡さない。
    const bool playPick = playSession.IsRunning() && workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Project) &&
                          viewportImageHovered && ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (playPick) {
        // 試玩中の画は游戏の相机で描かれている（編集の相机ではない）。
        const RENDER3D::ResolvedCameraFrame& frame = scene.GetResolvedCameraFrame();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (const SceneObjectId picked = viewportSelectionService_.PickObject(
                frame.valid ? frame.camera : scene.GetCamera(), sceneViewportRect, {mouse.x, mouse.y});
            picked.value != 0u) {
            SelectViewportObjects(scene, {picked}, EditorObjectSelectionMode::Replace);
        }
    }
    playSession.UpdateEmbeddedViewportInput(
        embeddedInputRegion, viewportImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !playPick);

    // 機位を選んでいる間は、その機位の画面を右下の小窓に出す（その機位から見ている時、演出の画面、試玩中は出さない）。
    // 小窓の上のクリックは選択や手柄に使わない。
    SceneObjectId insetCamera{};
    if (const GameObject* selected = context_.selection.selectedObject;
        context_.overlays.showCameraPreview && !playSession.IsRunning() && !cameraPath_ && !cameraPilot_.Active() &&
        !workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics) && selected != nullptr &&
        selected->GetComponent<CameraComponent>() != nullptr &&
        !(scene.IsEditorCameraPreviewActive() && scene.GetEditorCameraPreviewObjectId() == selected->GetDocumentId()))
        insetCamera = selected->GetDocumentId();
    const EDITOR::SceneViewportRect insetRect = cameraInset_.Layout(scene, insetCamera, sceneViewportRect);
    if (insetRect.width > 0.0f && insetRect.Contains({ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y}))
        viewportImageHovered = false;

    // 布景の手柄（主光・灯の範囲や角度）は選択より先にマウスを取る。
    if (lookDevActive && !playSession.IsRunning() &&
        UpdateLookDevHandles(scene, sceneViewportRect, viewportImageHovered, gameViewFocused, gizmoCapture)) {
        gizmoCapture = true;
    }
    // モジュールの視口工具が有効な間は、選択と変換の手柄より先にマウスを取る。
    if (projectWorkspaceHost_ != nullptr && !playSession.IsRunning() &&
        projectWorkspaceHost_->Extensions().UpdateViewport(scene.GetCamera(), sceneViewportRect, viewportImageHovered,
                                                           TIME::GetFrameContext().unscaledDt,
                                                           *ImGui::GetWindowDrawList())) {
        gizmoCapture = true;
    }
    const bool authoringInteractionEnabled = !playSession.IsRunning() && !lookDevPlacing;
    // 灯と機位の画面上の印。点選で網目より先に当たるよう、選択を更新する前に集める（診断の相机経路の間は描かない）。
    const auto objectIcons = !playSession.IsRunning() && !cameraPath_
                                 ? CollectObjectOverlayIcons(scene, context_.overlays, imageOrigin, imageSize)
                                 : std::vector<EDITOR::DOCUMENT_SCENE::ViewportObjectIcon>{};
    viewportSelectionService_.SetIconTargets(ObjectIconTargets(objectIcons));
    const EDITOR::SceneViewportRect blockedViewportRegion =
        insetRect.width > 0.0f ? insetRect : EDITOR::SceneViewportRect{-1.0f, -1.0f, 0.0f, 0.0f};
    const EDITOR::SceneViewportInteractionResult interaction =
        viewportSelectionService_.UpdateInput(scene.GetCamera(), sceneViewportRect, blockedViewportRegion,
                                              authoringInteractionEnabled, viewportImageHovered, gizmoCapture);
    const bool preserveContextSelection = interaction.openContextMenu && interaction.selections.size() == 1u &&
                                          context_.selection.IsObjectSelected(interaction.selections.front());
    if (interaction.selectionChanged && !preserveContextSelection) {
        SelectViewportObjects(scene, interaction.selections, interaction.selectionMode);
    }
    if (interaction.openContextMenu) {
        ImGui::OpenPopup("SceneViewportContextMenu");
    }

    if (!playSession.IsRunning()) {
        for (SceneObjectId objectId : context_.selection.GetSelectedObjectIds()) {
            viewportSelectionService_.DrawSelectionOutline(scene.GetCamera(), sceneViewportRect, objectId,
                                                           ImGui::GetWindowDrawList());
        }
        viewportSelectionService_.DrawMarquee(ImGui::GetWindowDrawList());
        DrawObjectOverlayIcons(objectIcons, context_.overlays, context_.selection.GetSelectedObjectIds(),
                               ImGui::GetIO().MousePos);
        if (cameraPilot_.Active()) {
            // 操縦中：視口の縁を機位の色で囲み、抜け方を左上に出す。
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 max{imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y};
            drawList->AddRect(imageOrigin, max, IM_COL32(101, 217, 255, 220), 0.0f, 0, 2.0f);
            const std::string hint = TEXT::Localize("cine.pilotActive");
            const ImVec2 textSize = ImGui::CalcTextSize(hint.c_str());
            const ImVec2 textMin{imageOrigin.x + 12.0f, imageOrigin.y + 12.0f};
            drawList->AddRectFilled(textMin, {textMin.x + textSize.x + 16.0f, textMin.y + textSize.y + 10.0f},
                                    IM_COL32(8, 12, 17, 205), 4.0f);
            drawList->AddText({textMin.x + 8.0f, textMin.y + 5.0f}, IM_COL32(170, 232, 255, 245), hint.c_str());
        }
        if (lookDevActive) {
            UpdateLookDevViewport(scene, sceneViewportRect, viewportImageHovered, gameViewFocused, gizmoCapture);
        }
        DrawViewportContextMenu(scene);
    }
    cameraInset_.Draw(scene, *ImGui::GetWindowDrawList());
    viewportTransformHistory_.EndFrame(scene.GetSceneDocument());

    if (playSession.IsEmbeddedRunning()) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 max{imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y};
        if (playSession.HasEmbeddedInput()) {
            drawList->AddRect(imageOrigin, max, IM_COL32(64, 205, 222, 230), 0.0f, 0, 2.0f);
        }
        const std::string hint = playSession.IsPaused()
                                     ? TEXT::Localize("ui.viewport.paused")
                                     : (playSession.HasEmbeddedInput() ? TEXT::Localize("ui.viewport.gameInputActive")
                                                                       : TEXT::Localize("ui.viewport.clickToControl"));
        const ImVec2 textSize = ImGui::CalcTextSize(hint.c_str());
        const ImVec2 textMin{imageOrigin.x + 12.0f, imageOrigin.y + 12.0f};
        const ImVec2 textMax{textMin.x + textSize.x + 16.0f, textMin.y + textSize.y + 10.0f};
        drawList->AddRectFilled(textMin, textMax, IM_COL32(8, 12, 17, 205), 4.0f);
        drawList->AddText(ImVec2(textMin.x + 8.0f, textMin.y + 5.0f), IM_COL32(220, 235, 240, 245), hint.c_str());
    } else if (!scene.GetSceneLoadError().empty()) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 textPos(imageOrigin.x + 12.0f, imageOrigin.y + 48.0f);
        drawList->AddText(nullptr, 0.0f, textPos, IM_COL32(255, 150, 120, 255), scene.GetSceneLoadError().c_str(),
                          nullptr, imageSize.x - 24.0f);
    } else if (!viewportDropMessage_.empty()) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 textPos{imageOrigin.x + 14.0f, imageOrigin.y + imageSize.y - 28.0f};
        drawList->AddText(textPos, IM_COL32(210, 226, 236, 230), viewportDropMessage_.c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace HIKARI
