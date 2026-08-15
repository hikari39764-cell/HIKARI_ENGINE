#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Sand/HIKARI_SandSurfaceComponent.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    using EDITOR::DOCUMENT_SCENE::DrawLightOverlayIcons;
    using EDITOR::DOCUMENT_SCENE::DrawRenderDebugViewCombo;
    using EDITOR::DOCUMENT_SCENE::DrawViewportDebugOptions;
    using EDITOR::HandleTransformGizmoShortcuts;

    void DocumentSceneEditorController::DrawGameViewportWindow(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        viewportTransformHistory_.BeginFrame();
        bool open = context_.windows.viewport.showGameView;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (!ImGui::Begin(
                TEXT::LocalizeWindow(
                    "ui.window.gameView", "Game View").c_str(),
                &open,
                flags)) {
            context_.windows.viewport.showGameView = open;
            playSession.ReleaseEmbeddedInput();
            EDITOR::ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
            viewportTransformHistory_.EndFrame(
                scene.GetSceneDocument());
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        context_.windows.viewport.showGameView = open;
        if (!open) {
            playSession.ReleaseEmbeddedInput();
        }

        const bool selectedObjectIsCamera =
            context_.selection.selectedObject != nullptr &&
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
                        previewRunning
                            ? EDITOR::EditorGlyph::Stop
                            : EDITOR::EditorGlyph::Play,
                        "EmbeddedPlay",
                        previewRunning
                            ? EDITOR::EditorButtonTone::Danger
                            : EDITOR::EditorButtonTone::Primary,
                        ImVec2(26.0f, 26.0f),
                        previewRunning
                            ? TEXT::Localize("ui.viewport.stopPlay").c_str()
                            : TEXT::Localize("ui.viewport.playInGameView").c_str())) {
                    ToggleGamePreview(scene, playSession);
                }
                ImGui::SameLine(0.0f, 3.0f);
                if (ImGui::ArrowButton("##PlayModeMenu", ImGuiDir_Down)) {
                    ImGui::OpenPopup("Play Mode Menu");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s",
                        TEXT::Localize("ui.viewport.playOptions").c_str());
                }
                if (ImGui::BeginPopup("Play Mode Menu")) {
                    if (ImGui::MenuItem(
                            TEXT::Localize(
                                "ui.viewport.playInNewWindow").c_str(),
                            nullptr,
                            false,
                            !previewRunning)) {
                        LaunchWindowedGamePreview(scene, playSession);
                    }
                    ImGui::EndPopup();
                }
                if (playSession.IsEmbeddedRunning()) {
                    ImGui::SameLine();
                    if (EDITOR::IconButton(
                            playSession.IsPaused()
                                ? EDITOR::EditorGlyph::Play
                                : EDITOR::EditorGlyph::Pause,
                            "EmbeddedPlayPause",
                            playSession.IsPaused()
                                ? EDITOR::EditorButtonTone::Primary
                                : EDITOR::EditorButtonTone::Neutral,
                            ImVec2(26.0f, 26.0f),
                            playSession.IsPaused()
                                ? TEXT::Localize("ui.viewport.resumePlay").c_str()
                                : TEXT::Localize("ui.viewport.pausePlay").c_str())) {
                        playSession.TogglePause();
                    }
                }
                if (playSession.GetState() != EDITOR::EditorPlayState::Stopped) {
                    ImGui::SameLine();
                    EDITOR::StatusText(
                        playSession.IsPaused()
                            ? TEXT::Localize("ui.viewport.paused").c_str()
                            : (previewRunning
                                ? TEXT::Localize("ui.viewport.playRunning").c_str()
                                : TEXT::Localize("ui.viewport.playStatus").c_str()),
                        previewRunning
                            ? EDITOR::EditorStatusTone::Ready
                            : (playSession.GetState() == EDITOR::EditorPlayState::Failed
                                ? EDITOR::EditorStatusTone::Error
                                : EDITOR::EditorStatusTone::Normal));
                    if (ImGui::IsItemHovered() &&
                        !playSession.GetStatusMessage().empty()) {
                        ImGui::SetTooltip(
                            "%s",
                            playSession.GetStatusMessage().c_str());
                    }
                }
                if (!previewRunning) {
                    ImGui::SameLine(0.0f, 8.0f);
                    const EDITOR::ViewportAuthoringToolbarResult authoringToolbar =
                        EDITOR::DrawViewportAuthoringToolbar(
                            context_,
                            selectedObjectIsCamera);
                    if (authoringToolbar.settingsRequested) {
                        ImGui::OpenPopup("ViewportToolSettingsPopup");
                    }
                    if (ImGui::BeginPopup("ViewportToolSettingsPopup")) {
                        ImGui::SeparatorText(
                            TEXT::Localize(
                                "ui.viewport.settingsViewport").c_str());
                        DrawViewportDebugOptions(
                            context_.overlays,
                            context_.viewportPerformance);
                        ImGui::SeparatorText(
                            TEXT::Localize(
                                "ui.viewport.settingsGizmos").c_str());
                        ImGui::Checkbox(
                            TEXT::Localize(
                                "ui.viewport.onlySelectedObject").c_str(),
                            &context_.gizmos.showOnlySelectedObject);
                        const ComponentGizmoRegistry& gizmoRegistry =
                            scene.GetComponentGizmoRegistry();
                        for (const ComponentGizmoProvider& provider :
                            gizmoRegistry.GetProviders()) {
                            bool visible = context_.gizmos.IsProviderVisible(
                                provider.providerId,
                                provider.defaultVisible);
                            if (ImGui::Checkbox(
                                    provider.displayName.c_str(),
                                    &visible)) {
                                context_.gizmos.SetProviderVisible(
                                    provider.providerId,
                                    visible);
                            }
                        }
                        ImGui::SeparatorText(
                            TEXT::Localize(
                                "ui.viewport.settingsSnap").c_str());
                        ImGui::DragFloat3(
                            TEXT::Localize(
                                "ui.viewport.translate").c_str(),
                            &context_.transformGizmo.translateSnap.x,
                            0.05f,
                            0.001f,
                            100.0f);
                        ImGui::DragFloat(
                            TEXT::Localize(
                                "ui.viewport.rotate").c_str(),
                            &context_.transformGizmo.rotateSnapDeg,
                            0.5f,
                            0.1f,
                            180.0f,
                            TEXT::Localize(
                                "ui.viewport.degFormat").c_str());
                        ImGui::DragFloat(
                            TEXT::Localize(
                                "ui.viewport.scale").c_str(),
                            &context_.transformGizmo.scaleSnap,
                            0.01f,
                            0.001f,
                            10.0f);
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine();
                    RENDER3D::RenderQualitySettings qualitySettings =
                        RENDER3D::GetRenderQualitySettings();
                    EDITOR::ToolbarLabel(
                        TEXT::Localize("ui.viewport.render").c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(118.0f);
                    if (ImGui::BeginCombo(
                        "##GameViewRenderResolution",
                        RENDER3D::RenderResolutionPresetLabel(qualitySettings.sceneResolution),
                        ImGuiComboFlags_NoArrowButton)) {
                        constexpr RENDER3D::RenderResolutionPreset presets[] = {
                            RENDER3D::RenderResolutionPreset::Viewport,
                            RENDER3D::RenderResolutionPreset::P720,
                            RENDER3D::RenderResolutionPreset::P1080,
                            RENDER3D::RenderResolutionPreset::P1440,
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
                    EDITOR::ToolbarLabel(
                        TEXT::Localize("ui.viewport.view").c_str());
                    ImGui::SameLine();
                    DrawRenderDebugViewCombo("##GameViewDebug", context_.viewportDebug, 144.0f);
                    if (context_.viewportDebug.renderView != RenderDebugView::None) {
                        ImGui::SameLine();
                        EDITOR::StatusText(
                            TEXT::Localize(
                                "ui.viewport.diagnosticOutput").c_str(),
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
        auto processViewportShortcuts =
            [&](bool viewportImageHovered) {
            if (playSession.IsRunning()) {
                return;
            }
            const bool viewportShortcutFocused =
                gameViewFocused || viewportImageHovered;
            HandleTransformGizmoShortcuts(
                context_.transformGizmo,
                viewportShortcutFocused);
            const bool standardCommandExecuted =
                commandRouter_.ProcessViewportShortcuts(
                    viewportShortcutFocused);
            if (!standardCommandExecuted &&
                EDITOR::CanUseEditorShortcut(
                    EDITOR::EditorShortcutScope::Viewport,
                    viewportShortcutFocused) &&
                context_.selection.selectedObject != nullptr &&
                ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                sceneInspectorPanel_.RequestRename(
                    *context_.selection.selectedObject);
            }
        };
        EDITOR::SetGameViewportInputRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y, gameViewFocused);
        SERVICES::SetEditorGameViewportSize(
            static_cast<int>(imageSize.x + 0.5f),
            static_cast<int>(imageSize.y + 0.5f),
            true);
        auto drawTransformGizmoOverlay = [&]() -> bool {
            bool gizmoCapture = false;
            const EDITOR::EditorViewportRect viewportRect{
                imageOrigin.x,
                imageOrigin.y,
                imageSize.x,
                imageSize.y
            };
            if (!ImGui::IsPopupOpen("SceneViewportContextMenu") &&
                context_.selection.selectedObject != nullptr &&
                !EDITOR::IsObjectEditorLocked(
                    scene.GetSceneDocument(),
                    context_.selection.GetActiveObjectId())) {
                EditorTransformGizmoState gizmoState = context_.transformGizmo;
                if (ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
                    gizmoState.snapEnabled = true;
                }
                const EDITOR::SceneSelectionTransformResult transformResult =
                    EDITOR::DrawSceneSelectionTransformGizmo(
                        scene,
                        context_.selection,
                        transformGizmo_,
                        gizmoState,
                        viewportRect);
                gizmoCapture = transformResult.gizmo.interacting;

                const bool multipleObjects =
                    context_.selection.GetSelectedObjectCount() > 1u;
                std::string historyLabel =
                    TEXT::Localize(
                        multipleObjects
                            ? "ui.history.moveObjects"
                            : "ui.history.moveObject");
                switch (gizmoState.operation) {
                case EditorTransformGizmoOperation::Rotate:
                    historyLabel = TEXT::Localize(
                        multipleObjects
                            ? "ui.history.rotateObjects"
                            : "ui.history.rotateObject");
                    break;
                case EditorTransformGizmoOperation::Scale:
                    historyLabel = TEXT::Localize(
                        multipleObjects
                            ? "ui.history.scaleObjects"
                            : "ui.history.scaleObject");
                    break;
                case EditorTransformGizmoOperation::Translate:
                default:
                    break;
                }
                if (!transformResult.historyLabelOverride.empty()) {
                    historyLabel = transformResult.historyLabelOverride;
                }
                viewportTransformHistory_.ObserveBeforeApply(
                    scene.GetSceneDocument(),
                    context_.selection.selectedObject->GetDocumentId(),
                    historyLabel.c_str(),
                    transformResult.gizmo.manipulating ||
                        transformResult.gizmo.changed,
                    context_.sceneDirty ||
                        scene.HasUnsavedSceneChanges());

                if (!transformResult.changedObjectIds.empty()) {
                    EDITOR::CommitSceneSelectionTransforms(
                        scene,
                        transformResult);
                    context_.sceneDirty = true;
                    scene.SetUnsavedSceneChanges(true);
                    viewportTransformHistory_.MarkChanged();
                }
            }

            EDITOR::SetGameViewportGizmoCapture(gizmoCapture);
            return gizmoCapture;
        };

        const EDITOR::SceneViewportRect sceneViewportRect{
            imageOrigin.x,
            imageOrigin.y,
            imageSize.x,
            imageSize.y
        };
        bool viewportImageHovered = false;
        bool gizmoCapture = false;
        const bool ready = POST::PostSystem::IsEditorViewportReady();
        const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv = POST::PostSystem::GetEditorViewportSrv();
        if (ready && viewportSrv.ptr != 0) {
            const ImTextureID textureId = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewportSrv.ptr));
            ImGui::Image(textureId, imageSize);
            viewportImageHovered = ImGui::IsItemHovered();
            processViewportShortcuts(viewportImageHovered);
            // Drop target 驍ｵ・ｺ繝ｻ・ｯ viewport image 驍ｵ・ｺ繝ｻ・ｮ鬨ｾ・ｶ繝ｻ・ｴ髯溷供・ｾ螽ｯ繝ｻ鬨ｾ蜈ｷ・ｽ・ｻ鬯ｪ・ｭ繝ｻ・ｲ驍ｵ・ｺ陷会ｽｱ・つ遶乗劼・ｽ・ｾ隶呵ｶ｣・ｽ・ｶ陞｢・ｹ郢晢ｽｻ overlay item 驍ｵ・ｺ繝ｻ・ｫ髯槭ｑ・ｽ・ｪ驛｢・ｧ闕ｳ蟯ｩ髮ｷ驍ｵ・ｺ繝ｻ・ｪ驍ｵ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
            if (!playSession.IsRunning()) {
                HandleGameViewportAssetDrop(scene);
                gizmoCapture = drawTransformGizmoOverlay();
            }
        } else {
            const ImVec2 max{ imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(imageOrigin, max, IM_COL32(8, 10, 13, 255));
            drawList->AddRect(imageOrigin, max, IM_COL32(80, 108, 124, 160), 4.0f, 0, 1.0f);
            drawList->AddText(
                ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f),
                IM_COL32(190, 205, 215, 255),
                TEXT::Localize(
                    "ui.viewport.waitingForTexture").c_str());
            ImGui::Dummy(imageSize);
            viewportImageHovered = ImGui::IsItemHovered();
            processViewportShortcuts(viewportImageHovered);
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
        if (playSession.IsEmbeddedRunning()) {
            if (playSession.HasEmbeddedInput()) {
                if (!gameViewFocused ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    playSession.ReleaseEmbeddedInput();
                } else {
                    playSession.UpdateEmbeddedInputRegion(
                        embeddedInputRegion);
                }
            } else if (!playSession.IsPaused() &&
                viewportImageHovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                playSession.CaptureEmbeddedInput(
                    embeddedInputRegion);
            }
        }

        const bool authoringInteractionEnabled =
            !playSession.IsRunning();
        const EDITOR::SceneViewportRect blockedViewportRegion{
            -1.0f,
            -1.0f,
            0.0f,
            0.0f
        };
        const EDITOR::SceneViewportInteractionResult interaction =
            viewportSelectionService_.UpdateInput(
                scene.GetCamera(),
                sceneViewportRect,
                blockedViewportRegion,
                authoringInteractionEnabled,
                viewportImageHovered,
                gizmoCapture);
        const bool preserveContextSelection =
            interaction.openContextMenu &&
            interaction.selections.size() == 1u &&
            context_.selection.IsObjectSelected(
                interaction.selections.front());
        if (interaction.selectionChanged &&
            !preserveContextSelection) {
            SelectViewportObjects(
                scene,
                interaction.selections,
                interaction.selectionMode);
        }
        if (interaction.openContextMenu) {
            ImGui::OpenPopup("SceneViewportContextMenu");
        }

        if (!playSession.IsRunning()) {
            for (SceneObjectId objectId :
                context_.selection.GetSelectedObjectIds()) {
                viewportSelectionService_.DrawSelectionOutline(
                    scene.GetCamera(),
                    sceneViewportRect,
                    objectId,
                    ImGui::GetWindowDrawList());
            }
            viewportSelectionService_.DrawMarquee(
                ImGui::GetWindowDrawList());
            DrawLightOverlayIcons(
                scene,
                context_.overlays,
                imageOrigin,
                imageSize);
            DrawViewportContextMenu(scene);
            HandleSandTerrainBrush(
                scene,
                sceneViewportRect,
                viewportImageHovered);
        }
        viewportTransformHistory_.EndFrame(scene.GetSceneDocument());

        if (playSession.IsEmbeddedRunning()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 max{
                imageOrigin.x + imageSize.x,
                imageOrigin.y + imageSize.y
            };
            if (playSession.HasEmbeddedInput()) {
                drawList->AddRect(
                    imageOrigin,
                    max,
                    IM_COL32(64, 205, 222, 230),
                    0.0f,
                    0,
                    2.0f);
            }
            const std::string hint =
                playSession.IsPaused()
                    ? TEXT::Localize("ui.viewport.paused")
                    : (playSession.HasEmbeddedInput()
                        ? TEXT::Localize("ui.viewport.gameInputActive")
                        : TEXT::Localize("ui.viewport.clickToControl"));
            const ImVec2 textSize =
                ImGui::CalcTextSize(hint.c_str());
            const ImVec2 textMin{
                imageOrigin.x + 12.0f,
                imageOrigin.y + 12.0f
            };
            const ImVec2 textMax{
                textMin.x + textSize.x + 16.0f,
                textMin.y + textSize.y + 10.0f
            };
            drawList->AddRectFilled(
                textMin,
                textMax,
                IM_COL32(8, 12, 17, 205),
                4.0f);
            drawList->AddText(
                ImVec2(textMin.x + 8.0f, textMin.y + 5.0f),
                IM_COL32(220, 235, 240, 245),
                hint.c_str());
        } else if (!viewportDropMessage_.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 textPos{ imageOrigin.x + 14.0f, imageOrigin.y + imageSize.y - 28.0f };
            drawList->AddText(textPos, IM_COL32(210, 226, 236, 230), viewportDropMessage_.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    void DocumentSceneEditorController::HandleSandTerrainBrush(
        DocumentSceneBase& scene,
        const EDITOR::SceneViewportRect& viewport,
        bool viewportHovered) {
#if defined(HIKARI_WITH_EDITOR)
        GameObject* selected = context_.selection.selectedObject;
        SandSurfaceComponent* sand =
            selected != nullptr
                ? selected->GetComponent<SandSurfaceComponent>()
                : nullptr;
        ProceduralMeshComponent* procedural =
            selected != nullptr
                ? selected->GetComponent<ProceduralMeshComponent>()
                : nullptr;

        const auto finishStroke = [&]() {
            if (!sandTerrainStrokeActive_ ||
                sand == nullptr ||
                selected == nullptr) {
                sandTerrainStrokeActive_ = false;
                return;
            }
            sandTerrainStrokeActive_ = false;

            nlohmann::json serialized = nlohmann::json::object();
            sand->Serialize(serialized);
            if (SceneObjectData* documentObject =
                    selectionSync_.FindDocumentObjectByRuntime(
                        scene,
                        selected)) {
                for (SceneComponentData& component :
                    documentObject->components) {
                    if (component.type != "SandSurfaceComponent") {
                        continue;
                    }
                    component.properties = serialized;
                    break;
                }
            }

            SceneObjectAuthoringHistoryRequest history{};
            history.label = "Sculpt Sand Terrain";
            history.beforeObjects = sandBrushBeforeObjects_;
            history.afterObjects = scene.GetSceneDocument().objects;
            history.beforeCamera = sandBrushBeforeCamera_;
            history.afterCamera = scene.GetSceneDocument().camera;
            history.dirtyBefore = sandBrushDirtyBefore_;
            history.runtimeWorldAffected = true;
            history.runtimeWorldRefreshRequired = true;
            CommitSceneObjectAuthoringHistory(
                scene,
                std::move(history));
        };

        if (sand == nullptr || procedural == nullptr) {
            finishStroke();
            return;
        }

        const ProceduralMeshSettings mesh =
            SanitizeProceduralMeshSettings(
                procedural->GetSettings());
        const MATH::Vec2 halfExtents{
            mesh.width * 0.5f,
            mesh.depth * 0.5f
        };

        if (!ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_B, false)) {
            sandTerrainSculptActive_ = !sandTerrainSculptActive_;
            finishStroke();
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(
            ImVec2(viewport.x + 14.0f, viewport.y + 14.0f),
            sandTerrainSculptActive_
                ? IM_COL32(250, 196, 96, 245)
                : IM_COL32(210, 226, 236, 210),
            sandTerrainSculptActive_
                ? "Sand Sculpt: ON (B) - drag to paint"
                : "Sand Sculpt: OFF (B)");

        const bool pointerDown =
            sandTerrainSculptActive_ &&
            viewportHovered &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemActive();
        if (!pointerDown) {
            finishStroke();
            return;
        }

        if (!sandTerrainStrokeActive_) {
            sandBrushBeforeObjects_ =
                scene.GetSceneDocument().objects;
            sandBrushBeforeCamera_ =
                scene.GetSceneDocument().camera;
            sandBrushDirtyBefore_ =
                context_.sceneDirty ||
                scene.HasUnsavedSceneChanges();
            sandTerrainStrokeActive_ = true;
        }

        const Camera3D& camera = scene.GetCamera();
        const MATH::Vec3 forward = MATH::Normalize(
            camera.GetTarget() - camera.GetPosition());
        const MATH::Vec3 right = MATH::Normalize(
            MATH::Cross(camera.GetUp(), forward));
        const MATH::Vec3 up = MATH::Normalize(
            MATH::Cross(forward, right));
        const float tanHalfFov = std::tan(
            (std::max)(camera.GetFovYRad(), 0.001f) * 0.5f);
        const float aspect = viewport.height > 0.0f
            ? viewport.width / viewport.height
            : camera.GetAspect();
        const ImVec2 mouse = ImGui::GetMousePos();
        const float ndcX =
            (mouse.x - viewport.x) /
                (std::max)(viewport.width, 1.0f) * 2.0f - 1.0f;
        const float ndcY =
            1.0f -
            (mouse.y - viewport.y) /
                (std::max)(viewport.height, 1.0f) * 2.0f;
        const MATH::Vec3 rayDirection = MATH::Normalize(
            forward +
            right * (ndcX * aspect * tanHalfFov) +
            up * (ndcY * tanHalfFov));
        const MATH::Vec3 rayOrigin = camera.GetPosition();

        const MATH::Mat4 world =
            selected->GetTransform().GetWorldMatrix();
        const MATH::Mat4 inverseWorld = MATH::Inverse(world);
        const MATH::Vec4 localOrigin4 = inverseWorld.TransformPoint({
            rayOrigin.x, rayOrigin.y, rayOrigin.z, 1.0f });
        const MATH::Vec4 localDir4 = inverseWorld.TransformPoint({
            rayDirection.x, rayDirection.y, rayDirection.z, 0.0f });
        const MATH::Vec3 localOrigin{
            localOrigin4.x, localOrigin4.y, localOrigin4.z };
        const MATH::Vec3 localDir{
            localDir4.x, localDir4.y, localDir4.z };

        if (std::abs(localDir.y) > 1.0e-6f) {
            const float t = -localOrigin.y / localDir.y;
            if (t >= 0.0f) {
                const MATH::Vec2 localXZ{
                    localOrigin.x + localDir.x * t,
                    localOrigin.z + localDir.z * t
                };
                if (std::abs(localXZ.x) <= halfExtents.x &&
                    std::abs(localXZ.y) <= halfExtents.y) {
                    sand->SculptTerrain(halfExtents, localXZ);
                }
            }
        }
#else
        (void)scene;
        (void)viewport;
        (void)viewportHovered;
#endif
    }

} // namespace HIKARI
