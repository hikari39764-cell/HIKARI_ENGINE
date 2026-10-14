#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"
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
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

using EDITOR::HandleTransformGizmoShortcuts;
using EDITOR::DOCUMENT_SCENE::DrawLightOverlayIcons;
using EDITOR::DOCUMENT_SCENE::DrawRenderDebugViewCombo;
using EDITOR::DOCUMENT_SCENE::DrawViewportDebugOptions;

void DocumentSceneEditorController::DrawGameViewportWindow(DocumentSceneBase& scene,
                                                           EDITOR::EditorPlaySession& playSession,
                                                           std::string_view stableWindowId) {
#if defined(HIKARI_WITH_EDITOR)
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
                if (ImGui::MenuItem(
                        TEXT::LocalizeWindow("editor.ui.developer.console.20aa93", "Developer Console").c_str(), "F10",
                        false, previewRunning)) {
                    runtimeDebugWindow_.Open();
                }
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
    auto drawTransformGizmoOverlay = [&]() -> bool {
        bool gizmoCapture = false;
        const EDITOR::EditorViewportRect viewportRect{imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y};
        if (!ImGui::IsPopupOpen("SceneViewportContextMenu") && context_.selection.selectedObject != nullptr &&
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
        // Drop target 驍ｵ・ｺ繝ｻ・ｯ viewport image 驍ｵ・ｺ繝ｻ・ｮ鬨ｾ・ｶ繝ｻ・ｴ髯溷供・ｾ螽ｯ繝ｻ鬨ｾ蜈ｷ・ｽ・ｻ鬯ｪ・ｭ繝ｻ・ｲ驍ｵ・ｺ陷会ｽｱ・つ遶乗劼・ｽ・ｾ隶呵ｶ｣・ｽ・ｶ陞｢・ｹ郢晢ｽｻ overlay item 驍ｵ・ｺ繝ｻ・ｫ髯槭ｑ・ｽ・ｪ驛｢・ｧ闕ｳ蟯ｩ髮ｷ驍ｵ・ｺ繝ｻ・ｪ驍ｵ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
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
        // Dummy 驍ｵ・ｺ郢晢ｽｻviewport 髯ｷ闌ｨ・ｽ・ｨ髣厄ｽｴ髦ｮ蜷ｶ繝ｻ hit rect 驍ｵ・ｺ繝ｻ・ｫ驍ｵ・ｺ繝ｻ・ｪ驛｢・ｧ闕ｵ譏ｶ陞ｺ驛｢・ｧ遶丞仰繝ｻ蟄･erlay 髫ｰ・ｰ陷諤懈・髯ｷ鮃ｹ莠らｫ翫・drop target 驍ｵ・ｺ繝ｻ・ｫ驍ｵ・ｺ陷ｷ・ｶ繝ｻ迢暦ｽｸ・ｲ郢晢ｽｻ
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
    playSession.UpdateEmbeddedViewportInput(embeddedInputRegion,
                                            viewportImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left));

    // 機位を選んでいる間は、その機位の画面を右下の小窓に出す（その機位から見ている時、演出の画面、試玩中は出さない）。
    // 小窓の上のクリックは選択や手柄に使わない。
    SceneObjectId insetCamera{};
    if (const GameObject* selected = context_.selection.selectedObject;
        context_.overlays.showCameraPreview && !playSession.IsRunning() && !cameraPath_ &&
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
    const bool authoringInteractionEnabled = !playSession.IsRunning() && !lookDevPlacing;
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
        if (!cameraPath_) // 診断の相机経路で動かしている間は辅助表示を描かない（Draw の cleanFrame と同じ）。
            DrawLightOverlayIcons(scene, context_.overlays, imageOrigin, imageSize);
        if (lookDevActive) {
            UpdateLookDevViewport(scene, sceneViewportRect, viewportImageHovered, gameViewFocused, gizmoCapture);
        }
        DrawViewportContextMenu(scene);
        if (!lookDevActive) {
            HandleTerrainBrush(scene, sceneViewportRect, viewportImageHovered);
        }
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
#else
    (void)scene;
    (void)playSession;
#endif
}

void DocumentSceneEditorController::HandleTerrainBrush(DocumentSceneBase& scene,
                                                       const EDITOR::SceneViewportRect& viewport,
                                                       bool viewportHovered) {
#if defined(HIKARI_WITH_EDITOR)
    GameObject* selected = context_.terrainTool.targetObjectId.value != 0u
                               ? scene.GetWorld().FindObject(context_.terrainTool.targetObjectId)
                               : nullptr;
    if (selected == nullptr || selected->GetComponent<TerrainSurfaceComponent>() == nullptr) {
        GameObject* activeSelection = context_.selection.selectedObject;
        if (activeSelection != nullptr && activeSelection->GetComponent<TerrainSurfaceComponent>() != nullptr) {
            selected = activeSelection;
            context_.terrainTool.targetObjectId = activeSelection->GetDocumentId();
        }
    }
    TerrainSurfaceComponent* terrain =
        selected != nullptr ? selected->GetComponent<TerrainSurfaceComponent>() : nullptr;
    ProceduralMeshComponent* procedural =
        selected != nullptr ? selected->GetComponent<ProceduralMeshComponent>() : nullptr;

    const auto toTerrainMode = [](EditorTerrainToolMode mode) {
        switch (mode) {
        case EditorTerrainToolMode::Lower: return TERRAIN::TerrainBrushMode::Lower;
        case EditorTerrainToolMode::Pit: return TERRAIN::TerrainBrushMode::Pit;
        case EditorTerrainToolMode::Smooth: return TERRAIN::TerrainBrushMode::Smooth;
        case EditorTerrainToolMode::Flatten: return TERRAIN::TerrainBrushMode::Flatten;
        case EditorTerrainToolMode::Mound: return TERRAIN::TerrainBrushMode::Mound;
        case EditorTerrainToolMode::Ridge: return TERRAIN::TerrainBrushMode::Ridge;
        case EditorTerrainToolMode::Terrace: return TERRAIN::TerrainBrushMode::Terrace;
        case EditorTerrainToolMode::Shaft: return TERRAIN::TerrainBrushMode::Shaft;
        case EditorTerrainToolMode::Noise: return TERRAIN::TerrainBrushMode::Noise;
        case EditorTerrainToolMode::Hole:
        case EditorTerrainToolMode::CliffOpening:
        case EditorTerrainToolMode::Ramp:
        case EditorTerrainToolMode::Raise:
        default: return TERRAIN::TerrainBrushMode::Raise;
        }
    };
    const auto getBrushRadius = [&]() -> float {
        if (terrain != nullptr) {
            return terrain->GetBrush().radius;
        }
        return 1.0f;
    };
    const auto setBrushRadius = [&](float radius) {
        if (terrain != nullptr) {
            terrain->SetBrushRadius(radius);
        }
    };

    const auto resetStrokeState = [&]() {
        terrainStrokeActive_ = false;
        terrainBrushStrokeChanged_ = false;
        terrainBrushTargetObjectId_ = {};
        terrainBrushTargetRuntimeHandle_ = {};
        terrainBrushTargetComponentId_ = {};
        terrainBrushPreviousLocalHit_ = {};
        terrainBrushHasPreviousLocalHit_ = false;
        terrainBrushDirtyBefore_ = false;
    };

    const auto finishStroke = [&]() {
        if (!terrainStrokeActive_) {
            return;
        }
        const SceneObjectId strokeObjectId = terrainBrushTargetObjectId_;
        const SceneComponentId strokeComponentId = terrainBrushTargetComponentId_;
        GameObject* strokeTarget = scene.GetWorld().FindObject(strokeObjectId);
        TerrainSurfaceComponent* strokeTerrain =
            strokeTarget != nullptr ? strokeTarget->GetComponent<TerrainSurfaceComponent>() : nullptr;
        if (strokeTerrain == nullptr || strokeTarget->GetRuntimeHandle() != terrainBrushTargetRuntimeHandle_) {
            // The runtime world changed while the stroke was active.
            // Runtime-only edits cannot be committed safely against a
            // replacement component, so restore the document projection.
            (void)selectionSync_.RebuildRuntimeWorldWithSelectionSync(scene, context_.selection,
                                                                      context_.nextSceneObjectId);
            resetStrokeState();
            return;
        }
        (void)strokeTerrain->EndGeometryEditBatch();
        const bool strokeChanged = terrainBrushStrokeChanged_;
        if (!strokeChanged) {
            nlohmann::json discardedBefore{};
            nlohmann::json discardedAfter{};
            (void)strokeTerrain->ConsumeGeometryEditDelta(discardedBefore, discardedAfter);
            resetStrokeState();
            return;
        }

        nlohmann::json beforeDelta = nlohmann::json::object();
        nlohmann::json afterDelta = nlohmann::json::object();
        if (!strokeTerrain->ConsumeGeometryEditDelta(beforeDelta, afterDelta)) {
            (void)selectionSync_.RebuildRuntimeWorldWithSelectionSync(scene, context_.selection,
                                                                      context_.nextSceneObjectId);
            resetStrokeState();
            return;
        }
        SceneObjectData* documentObject = FindSceneObjectData(scene.GetSceneDocument(), strokeObjectId);
        SceneComponentData* documentComponent =
            documentObject != nullptr ? FindSceneComponent(*documentObject, strokeComponentId) : nullptr;
        if (documentComponent == nullptr) {
            (void)selectionSync_.RebuildRuntimeWorldWithSelectionSync(scene, context_.selection,
                                                                      context_.nextSceneObjectId);
            resetStrokeState();
            return;
        }
        if (!EDITOR::ApplyTerrainSurfaceEditDelta(documentComponent->properties, afterDelta)) {
            (void)selectionSync_.RebuildRuntimeWorldWithSelectionSync(scene, context_.selection,
                                                                      context_.nextSceneObjectId);
            resetStrokeState();
            return;
        }

        std::string label = context_.terrainTool.paint != EditorTerrainPaintMode::None         ? "Paint Terrain Surface"
                            : context_.terrainTool.mode == EditorTerrainToolMode::Hole         ? "Edit Terrain Hole"
                            : context_.terrainTool.mode == EditorTerrainToolMode::CliffOpening ? "Cut Cliff Opening"
                            : context_.terrainTool.mode == EditorTerrainToolMode::Shaft        ? "Create Terrain Shaft"
                            : context_.terrainTool.mode == EditorTerrainToolMode::Noise        ? "Noise Terrain Surface"
                            : context_.terrainTool.mode == EditorTerrainToolMode::Ramp         ? "Create Terrain Ramp"
                                                                                       : "Sculpt Terrain Surface";
        CommitAppliedDocumentCommand(scene,
                                     EDITOR::MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
                                         std::move(label), strokeObjectId, strokeComponentId, std::move(beforeDelta),
                                         std::move(afterDelta), EDITOR::EditorDocumentImpact::RuntimeWorld),
                                     0, terrainBrushDirtyBefore_ && !documentHistory_.IsDirty(),
                                     EDITOR::EditorDocumentImpact::None);
        resetStrokeState();
    };

    const auto beginStroke = [&]() -> bool {
        if (terrainStrokeActive_) {
            return true;
        }
        if (selected == nullptr || terrain == nullptr) {
            return false;
        }
        SceneObjectData* documentObject = selectionSync_.FindDocumentObjectByRuntime(scene, selected);
        SceneComponentData* documentComponent = nullptr;
        if (documentObject != nullptr) {
            for (SceneComponentData& component : documentObject->components) {
                if (component.type == "TerrainSurfaceComponent") {
                    documentComponent = &component;
                    break;
                }
            }
        }
        // 履歴対象を安定 ID で確定できる場合だけ live mutation を
        // 始める。Undo 不能な地形編集を先に適用しない。
        if (documentComponent == nullptr || documentComponent->id.value == 0u) {
            return false;
        }
        terrainBrushDirtyBefore_ = context_.sceneDirty || scene.HasUnsavedSceneChanges();
        terrainBrushTargetObjectId_ = selected->GetDocumentId();
        terrainBrushTargetRuntimeHandle_ = selected->GetRuntimeHandle();
        terrainBrushTargetComponentId_ = documentComponent->id;
        terrainBrushStrokeChanged_ = false;
        terrainBrushPreviousLocalHit_ = {};
        terrainBrushHasPreviousLocalHit_ = false;
        terrain->BeginGeometryEditBatch();
        terrainStrokeActive_ = true;
        return true;
    };

    if (!workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::TerrainSurface)) {
        context_.terrainTool.active = false;
        finishStroke();
        return;
    }

    if (terrainStrokeActive_ && (selected == nullptr || selected->GetDocumentId() != terrainBrushTargetObjectId_)) {
        finishStroke();
    }

    if (terrain == nullptr || procedural == nullptr) {
        finishStroke();
        context_.terrainTool.active = false;
        return;
    }

    const Camera3D& camera = scene.GetCamera();
    const ImVec2 mouse = ImGui::GetMousePos();
    EDITOR::EditorPointerRay pointerRay{};
    const float normalizedX = (mouse.x - viewport.x) / (std::max)(viewport.width, 1.0f);
    const float normalizedY = (mouse.y - viewport.y) / (std::max)(viewport.height, 1.0f);
    if (!EDITOR::BuildEditorPointerRay(camera, normalizedX, normalizedY, pointerRay)) {
        context_.terrainTool.hasHit = false;
        finishStroke();
        return;
    }

    const auto setTool = [&](EditorTerrainToolMode mode) {
        context_.terrainTool.mode = mode;
        context_.terrainTool.active = true;
        terrain->SetBrushMode(toTerrainMode(mode));
    };

    if (!ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
            setTool(EditorTerrainToolMode::Raise);
        } else if (ImGui::IsKeyPressed(ImGuiKey_2, false)) {
            setTool(EditorTerrainToolMode::Lower);
        } else if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
            setTool(EditorTerrainToolMode::Smooth);
        } else if (ImGui::IsKeyPressed(ImGuiKey_4, false)) {
            setTool(EditorTerrainToolMode::Flatten);
        } else if (ImGui::IsKeyPressed(ImGuiKey_5, false)) {
            setTool(EditorTerrainToolMode::Mound);
        } else if (ImGui::IsKeyPressed(ImGuiKey_6, false)) {
            setTool(EditorTerrainToolMode::Pit);
        } else if (ImGui::IsKeyPressed(ImGuiKey_7, false)) {
            setTool(EditorTerrainToolMode::Ridge);
        } else if (ImGui::IsKeyPressed(ImGuiKey_8, false)) {
            setTool(EditorTerrainToolMode::Terrace);
        } else if (ImGui::IsKeyPressed(ImGuiKey_9, false)) {
            setTool(EditorTerrainToolMode::Shaft);
        } else if (ImGui::IsKeyPressed(ImGuiKey_0, false)) {
            setTool(EditorTerrainToolMode::Hole);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            context_.terrainTool.active = false;
            context_.terrainTool.rampAnchorSet = false;
            context_.terrainTool.sampleHeightRequested = false;
            finishStroke();
        }
        if (context_.terrainTool.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) {
                setBrushRadius((std::max)(getBrushRadius() * 0.9f, 0.05f));
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) {
                setBrushRadius(getBrushRadius() / 0.9f);
            }
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(ImVec2(viewport.x + 14.0f, viewport.y + 14.0f),
                      context_.terrainTool.active ? IM_COL32(250, 196, 96, 245) : IM_COL32(210, 226, 236, 210),
                      context_.terrainTool.active ? TEXT::Localize("ui.terrain.brushHint").c_str()
                                                  : TEXT::Localize("ui.terrain.brushHint").c_str());

    if (!context_.terrainTool.active) {
        context_.terrainTool.hasHit = false;
        finishStroke();
        return;
    }

    // Terrain query/bake は高価なので、viewport 外の Inspector 操作中は
    // hover preview を更新しない。ドラッグが外へ出た場合も従来どおり
    // その場で stroke を確定する。
    if (!viewportHovered) {
        context_.terrainTool.hasHit = false;
        finishStroke();
        return;
    }

    const bool pointerDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool pointerClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    MATH::Vec3 worldHit{};
    MATH::Vec3 worldHitNormal{};
    const bool hasHit =
        terrain->RaycastWorld(pointerRay.origin, pointerRay.direction, 500.0f, worldHit, worldHitNormal, true);

    if (hasHit) {
        context_.terrainTool.hasHit = true;
        context_.terrainTool.hitPoint = worldHit;

        const TERRAIN::TerrainBrush previewBrush = terrain->GetBrush();
        const EditorTerrainPaintMode previewPaint = context_.terrainTool.paint;
        ImU32 brushColor = IM_COL32(250, 196, 96, 245);
        if (previewPaint == EditorTerrainPaintMode::Snow) {
            brushColor = IM_COL32(240, 244, 250, 245);
        } else if (previewPaint == EditorTerrainPaintMode::Dirt) {
            brushColor = IM_COL32(170, 120, 70, 245);
        } else if (previewPaint == EditorTerrainPaintMode::Grass) {
            brushColor = IM_COL32(110, 190, 80, 245);
        } else if (previewPaint == EditorTerrainPaintMode::Rock) {
            brushColor = IM_COL32(170, 178, 190, 245);
        } else if (previewPaint == EditorTerrainPaintMode::Erase) {
            brushColor = IM_COL32(240, 90, 90, 245);
        } else if (context_.terrainTool.mode == EditorTerrainToolMode::Lower ||
                   context_.terrainTool.mode == EditorTerrainToolMode::Pit ||
                   context_.terrainTool.mode == EditorTerrainToolMode::Shaft ||
                   context_.terrainTool.mode == EditorTerrainToolMode::Hole ||
                   context_.terrainTool.mode == EditorTerrainToolMode::CliffOpening) {
            brushColor = IM_COL32(250, 130, 90, 245);
        } else if (context_.terrainTool.mode == EditorTerrainToolMode::Smooth) {
            brushColor = IM_COL32(120, 220, 160, 245);
        } else if (context_.terrainTool.mode == EditorTerrainToolMode::Flatten ||
                   context_.terrainTool.mode == EditorTerrainToolMode::Ramp) {
            brushColor = IM_COL32(200, 170, 250, 245);
        } else if (context_.terrainTool.mode == EditorTerrainToolMode::Noise) {
            brushColor = IM_COL32(245, 210, 90, 245);
        }

        // ブラシを実際の高さ場に沿って貼り付けて描く。
        const float brushRadiusLocal = getBrushRadius();
        const MATH::Vec3 localHit = terrain->WorldToLocal(worldHit);
        const MATH::Vec3 localHitNormal = terrain->WorldNormalToLocal(worldHitNormal);
        constexpr float kCliffOpeningMaximumUpDot = 0.82f;
        const bool cliffOpeningSurface = std::fabs(worldHitNormal.y) <= kCliffOpeningMaximumUpDot;
        const MATH::Mat4 viewProjection = camera.GetViewProj();
        const auto projectToScreen = [&](const MATH::Vec3& worldPos, ImVec2& outScreen) -> bool {
            const MATH::Vec4 clip = viewProjection.TransformPoint({worldPos.x, worldPos.y, worldPos.z, 1.0f});
            if (clip.w <= 1.0e-4f) {
                return false;
            }
            const float invW = 1.0f / clip.w;
            outScreen.x = viewport.x + (clip.x * invW * 0.5f + 0.5f) * viewport.width;
            outScreen.y = viewport.y + (1.0f - (clip.y * invW * 0.5f + 0.5f)) * viewport.height;
            return true;
        };
        const auto drawGroundRing = [&](float ringRadius, int segments, float thickness) {
            ImVec2 previous{};
            bool previousValid = false;
            for (int segment = 0; segment <= segments; ++segment) {
                const float angle = (static_cast<float>(segment) / segments) * 6.28318530718f;
                MATH::Vec3 ringLocal{localHit.x + std::cos(angle) * ringRadius, 0.0f,
                                     localHit.z + std::sin(angle) * ringRadius};
                float ringHeight = 0.0f;
                if (!terrain->SampleLocalSurfaceHeight(ringLocal, ringHeight)) {
                    ringHeight = 0.0f;
                }
                ringLocal.y = ringHeight;
                MATH::Vec3 ringWorld = terrain->LocalToWorld(ringLocal);
                ringWorld = ringWorld + worldHitNormal * 0.02f;
                ImVec2 screenPoint{};
                const bool valid = projectToScreen(ringWorld, screenPoint);
                if (valid && previousValid) {
                    drawList->AddLine(previous, screenPoint, brushColor, thickness);
                }
                previous = screenPoint;
                previousValid = valid;
            }
        };
        if (context_.terrainTool.mode != EditorTerrainToolMode::CliffOpening &&
            context_.terrainTool.mode != EditorTerrainToolMode::Ramp) {
            drawGroundRing(brushRadiusLocal, 48, 2.0f);
            const float innerRadius = brushRadiusLocal * std::clamp(previewBrush.hardness, 0.0f, 1.0f);
            if (innerRadius > 0.01f) {
                drawGroundRing(innerRadius, 32, 1.0f);
            }
        }
        ImVec2 centerScreen{};
        if (projectToScreen(worldHit, centerScreen)) {
            drawList->AddCircleFilled(centerScreen, 3.0f, brushColor, 12);
        }

        if (context_.terrainTool.mode == EditorTerrainToolMode::Ramp && context_.terrainTool.rampAnchorSet) {
            const MATH::Vec3 rampStartWorld = terrain->LocalToWorld(context_.terrainTool.rampStartLocal);
            ImVec2 rampStartScreen{};
            ImVec2 rampEndScreen{};
            if (projectToScreen(rampStartWorld, rampStartScreen) && projectToScreen(worldHit, rampEndScreen)) {
                drawList->AddLine(rampStartScreen, rampEndScreen, brushColor, 3.0f);
                drawList->AddCircleFilled(rampStartScreen, 4.0f, brushColor, 12);
            }
        }

        if (context_.terrainTool.mode == EditorTerrainToolMode::CliffOpening && cliffOpeningSurface) {
            MATH::Vec3 openingRight = MATH::Cross({0.0f, 1.0f, 0.0f}, localHitNormal);
            if (MATH::Dot(openingRight, openingRight) <= 1.0e-8f) {
                openingRight = {1.0f, 0.0f, 0.0f};
            }
            openingRight = MATH::Normalize(openingRight);
            const MATH::Vec3 openingUp = MATH::Normalize(MATH::Cross(localHitNormal, openingRight));
            const float halfWidth = context_.terrainTool.openingWidth * 0.5f;
            const float halfHeight = context_.terrainTool.openingHeight * 0.5f;
            std::array<MATH::Vec3, 4u> corners{
                localHit - openingRight * halfWidth - openingUp * halfHeight,
                localHit + openingRight * halfWidth - openingUp * halfHeight,
                localHit + openingRight * halfWidth + openingUp * halfHeight,
                localHit - openingRight * halfWidth + openingUp * halfHeight,
            };
            std::array<ImVec2, 4u> screens{};
            bool valid = true;
            for (std::size_t index = 0u; index < corners.size(); ++index) {
                corners[index] = terrain->LocalToWorld(corners[index]) + worldHitNormal * 0.025f;
                valid = projectToScreen(corners[index], screens[index]) && valid;
            }
            if (valid) {
                for (std::size_t index = 0u; index < screens.size(); ++index) {
                    drawList->AddLine(screens[index], screens[(index + 1u) % screens.size()], brushColor, 2.5f);
                }
            }
        }

        if (context_.terrainTool.sampleHeightRequested && pointerClicked) {
            context_.terrainTool.sampleHeightRequested = false;
            context_.terrainTool.sampledHeight = localHit.y;
            context_.terrainTool.sampledHeightReady = true;
            finishStroke();
            return;
        }

        if (context_.terrainTool.mode == EditorTerrainToolMode::CliffOpening) {
            finishStroke();
            if (!pointerClicked) {
                return;
            }
            if (!cliffOpeningSurface) {
                viewportDropMessage_ = TEXT::Localize("ui.terrain.openingRequiresWall");
                return;
            }
            if (beginStroke()) {
                TERRAIN::TerrainOpeningSpec opening{};
                opening.center = localHit;
                opening.normal = localHitNormal;
                opening.width = context_.terrainTool.openingWidth;
                opening.height = context_.terrainTool.openingHeight;
                opening.depth = context_.terrainTool.openingDepth;
                opening.feather = context_.terrainTool.openingFeather;
                terrainBrushStrokeChanged_ = terrain->ApplyOpeningAtLocal(opening);
                finishStroke();
            }
            return;
        }

        if (context_.terrainTool.mode == EditorTerrainToolMode::Ramp) {
            finishStroke();
            if (!pointerClicked) {
                return;
            }
            if (!context_.terrainTool.rampAnchorSet) {
                context_.terrainTool.rampStartLocal = localHit;
                context_.terrainTool.rampAnchorSet = true;
                return;
            }
            if (beginStroke()) {
                TERRAIN::TerrainRampSpec ramp{};
                ramp.start = context_.terrainTool.rampStartLocal;
                ramp.end = localHit;
                ramp.halfWidth = context_.terrainTool.rampHalfWidth;
                ramp.shoulderWidth = context_.terrainTool.rampShoulderWidth;
                ramp.blendAmount = context_.terrainTool.rampBlendAmount;
                terrainBrushStrokeChanged_ = terrain->ApplyRampAtLocal(ramp);
                finishStroke();
            }
            context_.terrainTool.rampAnchorSet = false;
            return;
        }

        const EditorTerrainPaintMode paint = context_.terrainTool.paint;
        if (pointerDown && !terrainStrokeActive_) {
            if (beginStroke() && context_.terrainTool.mode == EditorTerrainToolMode::Shaft) {
                TERRAIN::TerrainBrush shaftBrush = terrain->GetBrush();
                shaftBrush.flattenHeight = localHit.y - shaftBrush.shaftDepth;
                terrain->SetBrush(shaftBrush);
            }
        }

        if (pointerDown && terrainStrokeActive_) {
            const TERRAIN::TerrainBrush strokeBrush = terrain->GetBrush();
            const float cellSize =
                terrain->GetChunkWorldSize() / static_cast<float>((std::max)(terrain->GetChunkCells(), 1u));
            const float brushSpacing = (std::max)(cellSize, strokeBrush.radius * 0.22f);
            const float segmentDx = localHit.x - terrainBrushPreviousLocalHit_.x;
            const float segmentDz = localHit.z - terrainBrushPreviousLocalHit_.z;
            const float segmentLength =
                terrainBrushHasPreviousLocalHit_ ? std::sqrt(segmentDx * segmentDx + segmentDz * segmentDz) : 0.0f;
            const uint32_t sampleCount =
                terrainBrushHasPreviousLocalHit_
                    ? (std::max)(1u,
                                 static_cast<uint32_t>(std::ceil(segmentLength / (std::max)(brushSpacing, 1.0e-4f))))
                    : 1u;
            const float frameDeltaSeconds = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
            const float sampleDeltaSeconds = frameDeltaSeconds / static_cast<float>(sampleCount);

            TERRAIN::TerrainBrushMode mode = toTerrainMode(context_.terrainTool.mode);
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift) {
                if (mode == TERRAIN::TerrainBrushMode::Raise) {
                    mode = TERRAIN::TerrainBrushMode::Lower;
                } else if (mode == TERRAIN::TerrainBrushMode::Lower) {
                    mode = TERRAIN::TerrainBrushMode::Raise;
                } else if (mode == TERRAIN::TerrainBrushMode::Mound) {
                    mode = TERRAIN::TerrainBrushMode::Pit;
                } else if (mode == TERRAIN::TerrainBrushMode::Pit) {
                    mode = TERRAIN::TerrainBrushMode::Mound;
                }
            }
            if (io.KeyCtrl) {
                mode = TERRAIN::TerrainBrushMode::Smooth;
            }
            terrain->SetBrushMode(mode);

            bool frameChanged = false;
            for (uint32_t sampleIndex = 1u; sampleIndex <= sampleCount; ++sampleIndex) {
                const float t = terrainBrushHasPreviousLocalHit_
                                    ? static_cast<float>(sampleIndex) / static_cast<float>(sampleCount)
                                    : 1.0f;
                const MATH::Vec3 sampleLocal =
                    terrainBrushHasPreviousLocalHit_
                        ? terrainBrushPreviousLocalHit_ + (localHit - terrainBrushPreviousLocalHit_) * t
                        : localHit;
                bool sampleChanged = false;
                switch (paint) {
                case EditorTerrainPaintMode::Sand:
                    sampleChanged = terrain->PaintLayerAtLocal("sand", sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::Snow:
                    sampleChanged = terrain->PaintLayerAtLocal("snow", sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::Dirt:
                    sampleChanged = terrain->PaintLayerAtLocal("dirt", sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::Grass:
                    sampleChanged = terrain->PaintLayerAtLocal("grass", sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::Rock:
                    sampleChanged = terrain->PaintLayerAtLocal("rock", sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::Erase:
                    sampleChanged = terrain->PaintCoverageAtLocal(0.0f, sampleLocal, sampleDeltaSeconds);
                    break;
                case EditorTerrainPaintMode::None:
                default:
                    if (context_.terrainTool.mode == EditorTerrainToolMode::Hole) {
                        sampleChanged =
                            terrain->PaintCoverageAtLocal(io.KeyShift ? 1.0f : 0.0f, sampleLocal, sampleDeltaSeconds);
                    } else {
                        sampleChanged = terrain->SculptAtLocal(sampleLocal, sampleDeltaSeconds);
                    }
                    break;
                }
                frameChanged = frameChanged || sampleChanged;
            }
            terrainBrushPreviousLocalHit_ = localHit;
            terrainBrushHasPreviousLocalHit_ = true;
            terrainBrushStrokeChanged_ = terrainBrushStrokeChanged_ || frameChanged;
        }
    } else {
        context_.terrainTool.hasHit = false;
        terrainBrushHasPreviousLocalHit_ = false;
    }

    if (!pointerDown) {
        finishStroke();
    }
#else
    (void)scene;
    (void)viewport;
    (void)viewportHovered;
#endif
}

} // namespace HIKARI
