#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void VfxWorkspaceController::DrawPreviewWindow(
        EditorViewInstance& previewView,
        float deltaSeconds) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin((VfxUiText("Preview") + "###Vfx/Preview").c_str())) {
            RENDER3D::EDITORVIEW::ClearRequest(
                previewView.renderViewId);
            ImGui::End();
            return;
        }

        if (IconButton(EditorGlyph::Camera, "VfxView", EditorButtonTone::Quiet,
                {28,28}, VfxUiText("View").c_str())) ImGui::OpenPopup("VfxView");
        if (ImGui::BeginPopup("VfxView")) {
        if (ImGui::Button(VfxUiText("Perspective").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Perspective);
            ++cameraRevision_; cameraCutPending_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Front").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Front);
            ++cameraRevision_; cameraCutPending_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Top").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Top);
            ++cameraRevision_; cameraCutPending_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(TEXT::Localize("ui.vfx.view.clean").c_str(), &previewCleanView_)) previewNeedsRedraw_ = true;
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.hud").c_str(), &showPreviewHud_);
        if (ImGui::Checkbox(TEXT::Localize("ui.vfx.view.bypassAdjustments").c_str(), &previewBypassAdjustments_))
            NotifyEmitterVisibilityChanged();
        ImGui::Separator();
        ImGui::Checkbox(VfxUiText("Grid").c_str(), &showPreviewGrid_);
        ImGui::SameLine();
        ImGui::Checkbox(VfxUiText("Axes").c_str(), &showPreviewAxes_);
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.path").c_str(), &timelineEdit_.showMotionPath);
        ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Light, "VfxLighting", EditorButtonTone::Quiet,
                {28,28}, VfxUiText("Lighting").c_str())) ImGui::OpenPopup("VfxPreviewLighting");
        if (ImGui::BeginPopup("VfxPreviewLighting")) {
            ImGui::TextUnformatted(VfxUiText("Preview lighting (not saved in the effect)").c_str());
            const auto backdrop = [&](const char* key, float value) {
                if (ImGui::SmallButton(TEXT::Localize(key).c_str())) {
                    auto& sky = previewEnvironment_->sky;
                    sky.zenithColor = sky.horizonColor = sky.groundColor = {value, value, value};
                    previewNeedsRedraw_ = true;
                }
            };
            backdrop("ui.vfx.view.dark", 0.025f); ImGui::SameLine();
            backdrop("ui.vfx.view.gray", 0.18f); ImGui::SameLine();
            backdrop("ui.vfx.view.light", 0.85f);
            bool changed = ImGui::SliderFloat(VfxUiText("Direction").c_str(), &previewLightYaw_, -180.0f, 180.0f, "%.0f deg");
            changed |= ImGui::SliderFloat(VfxUiText("Brightness").c_str(), &previewLightIntensity_, 0.0f, 5.0f, "%.2f");
            auto& bloom = previewEnvironment_->bloom;
            ImGui::Separator();
            changed |= ImGui::Checkbox("Bloom", &bloom.enabled);
            changed |= ImGui::SliderFloat((VfxUiText("Threshold") + "##Bloom").c_str(), &bloom.threshold, 0.0f, 4.0f, "%.2f");
            changed |= ImGui::SliderFloat((VfxUiText("Intensity") + "##Bloom").c_str(), &bloom.intensity, 0.0f, 2.0f, "%.2f");
            changed |= ImGui::SliderFloat((VfxUiText("Radius") + "##Bloom").c_str(), &bloom.radius, 0.25f, 3.0f, "%.2f");
            if (changed && !previewLighting_->lights.empty()) {
                previewNeedsRedraw_ = true;
                const float yaw = previewLightYaw_ * 0.01745329252f;
                auto& key = previewLighting_->lights.front();
                key.direction = {std::sin(yaw)*0.6f, -0.8f, std::cos(yaw)*0.6f};
                key.intensity = previewLightIntensity_;
            }
            ImGui::TextWrapped(VfxUiText("StandardLit and CustomLit respond to this light. Unlit effects keep their authored color.").c_str());
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const auto* selectedLayer = FindEmitter(selection_.emitterId);
        const bool adjustmentSelected = selectedLayer && selectedLayer->kind == VFX::VfxLayerKind::Adjustment;
        ImGui::BeginDisabled(adjustmentSelected);
        if (IconButton(EditorGlyph::Focus, "Focus", EditorButtonTone::Quiet, {28,28},
                (VfxUiText("Focus") + " (F)").c_str())) {
            (void)FocusSelectedEmitter(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (IconToggleButton(EditorGlyph::Compact, "CleanView", previewCleanView_, {28,28},
                TEXT::Localize("ui.vfx.view.clean").c_str())) {
            previewCleanView_ = !previewCleanView_;
            previewNeedsRedraw_ = true;
        }
        ImGui::SameLine();
        if (IconToggleButton(EditorGlyph::Vfx, "BypassAdjustments", previewBypassAdjustments_, {28,28},
                TEXT::Localize("ui.vfx.view.bypassAdjustments").c_str())) {
            previewBypassAdjustments_ = !previewBypassAdjustments_;
            NotifyEmitterVisibilityChanged();
        }
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Help, "PreviewHelp", EditorButtonTone::Quiet, {28,28},
                TEXT::Localize("ui.vfx.hud.shortcuts").c_str())) ImGui::OpenPopup("PreviewHelp");
        if (ImGui::BeginPopup("PreviewHelp")) {
            ImGui::TextUnformatted(TEXT::Localize("ui.vfx.hud.shortcuts").c_str());
            ImGui::TextUnformatted(TEXT::Localize("ui.vfx.view.navigation").c_str());
            ImGui::EndPopup();
        }
        if (adjustmentSelected) {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.vfx.view.adjustmentScope").c_str());
        } else {
        const char* operations[] = {"Move", "Rotate", "Scale"};
        const EditorGlyph glyphs[]{EditorGlyph::Translate, EditorGlyph::Rotate, EditorGlyph::Scale};
        const char* shortcuts[]{" (W)", " (E)", " (R)"};
        for (int i = 0; i < 3; ++i) {
            if (i != 0) ImGui::SameLine(0, 2);
            if (IconToggleButton(glyphs[i], operations[i], static_cast<int>(layerGizmoState_.operation) == i,
                    {28,28}, (VfxUiText(operations[i]) + shortcuts[i]).c_str()))
                layerGizmoState_.operation = static_cast<EditorTransformGizmoOperation>(i);
        }
        ImGui::SameLine();
        const bool world = layerGizmoState_.mode == EditorTransformGizmoMode::World;
        if (IconButton(world ? EditorGlyph::WorldSpace : EditorGlyph::LocalSpace, "TransformSpace",
                EditorButtonTone::Quiet, {28,28}, VfxUiText(world ? "World (X)" : "Local (X)").c_str()))
            layerGizmoState_.mode = layerGizmoState_.mode == EditorTransformGizmoMode::World ? EditorTransformGizmoMode::Local : EditorTransformGizmoMode::World;
        ImGui::SameLine();
        if (IconToggleButton(EditorGlyph::Snap, "TransformSnap", layerGizmoState_.snapEnabled, {28,28},
                TEXT::Localize("ui.vfx.view.snap").c_str())) layerGizmoState_.snapEnabled = !layerGizmoState_.snapEnabled;
        }
        if (!adjustmentSelected) ImGui::SameLine();
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.autoKey").c_str(), &timelineEdit_.autoKey);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TEXT::Localize("ui.vfx.view.editModeHelp").c_str());

        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, 180.0f);
        canvasSize.y = (std::max)(canvasSize.y, 180.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            origin,
            { origin.x + canvasSize.x, origin.y + canvasSize.y },
            IM_COL32(11, 14, 20, 255));

        previewView.extent.width = static_cast<uint32_t>(canvasSize.x);
        previewView.extent.height = static_cast<uint32_t>(canvasSize.y);
        const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
            RENDER3D::EDITORVIEW::GetOutput(previewView.renderViewId);
        const bool ready = output.ready && output.colorSrv.ptr != 0u;
        ImGui::SetCursorScreenPos(origin);
        if (ready) {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(
                    static_cast<uintptr_t>(output.colorSrv.ptr)),
                canvasSize);
        } else {
            ImGui::InvisibleButton(
                "##VfxPreviewCanvas",
                canvasSize,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);
            const char* message = output.status ==
                    RENDER3D::EDITORVIEW::EditorInteractiveViewStatus::Failed
                ? RENDER3D::EDITORVIEW::
                    GetEditorInteractiveViewFailureMessage(
                        output.failure).data()
                : "Preparing GPU particle preview...";
            const ImVec2 textSize = ImGui::CalcTextSize(message);
            drawList->AddText(
                {
                    origin.x + (canvasSize.x - textSize.x) * 0.5f,
                    origin.y + (canvasSize.y - textSize.y) * 0.5f,
                },
                IM_COL32(173, 188, 206, 255),
                message);
        }

        const bool compileFailed = VFX::HasVfxErrors(diagnostics_);
        if (!document_.IsOpen() || compileFailed) {
            const char* message = !document_.IsOpen()
                ? "Open or create a VFX asset"
                : "Fix compile errors to update the preview";
            const ImVec2 textSize = ImGui::CalcTextSize(message);
            const ImVec2 padding{ 14.0f, 9.0f };
            const ImVec2 boxMinimum{
                origin.x + (canvasSize.x - textSize.x) * 0.5f - padding.x,
                origin.y + 18.0f
            };
            const ImVec2 boxMaximum{
                boxMinimum.x + textSize.x + padding.x * 2.0f,
                boxMinimum.y + textSize.y + padding.y * 2.0f
            };
            drawList->AddRectFilled(
                boxMinimum,
                boxMaximum,
                compileFailed
                    ? IM_COL32(91, 35, 35, 230)
                    : IM_COL32(28, 37, 50, 230),
                5.0f);
            drawList->AddText(
                { boxMinimum.x + padding.x,
                  boxMinimum.y + padding.y },
                compileFailed
                    ? IM_COL32(255, 178, 170, 255)
                    : IM_COL32(193, 205, 220, 255),
                message);
        }

        const bool canvasVisible = ImGui::IsItemVisible();
        const bool hovered = ImGui::IsItemHovered();
        if (!layerGizmoEditing_ && !timelineEdit_.baseline) {
            HandleTransformGizmoShortcuts(layerGizmoState_, hovered);
            if (!adjustmentSelected && hovered && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) (void)FocusSelectedEmitter(statusMessage_);
        }
        std::string pickedLayer;
        if (hovered && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyPressed(ImGuiKey_H)) { previewCleanView_ = !previewCleanView_; previewNeedsRedraw_ = true; }
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                if (!previewPlaying_) (void)PlayPreview(statusMessage_);
                else PausePreview(!previewPaused_);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) (void)StepPreviewFrame(-1, statusMessage_);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) (void)StepPreviewFrame(1, statusMessage_);
        }
        bool gizmoInteracting = false;
        if (!previewCleanView_) {
            gizmoInteracting = DrawLayerHandles(
                {origin.x, origin.y, canvasSize.x, canvasSize.y}, hovered, layerGizmoEditing_, pickedLayer);
            if (!gizmoInteracting)
                gizmoInteracting = DrawLayerGizmo({origin.x, origin.y, canvasSize.x, canvasSize.y});
        }
        if (showPreviewHud_ && !previewCleanView_ && ready && canvasSize.x > 240) {
            char timing[96];
            const auto stateLabel = TEXT::Localize(!previewPlaying_ ? "ui.vfx.hud.stopped" : previewPaused_ ? "ui.vfx.hud.paused" : "ui.vfx.hud.playing");
            std::snprintf(timing, sizeof(timing), "%s  %.2fs / %.2fs  |  %u f",
                stateLabel.c_str(),
                previewElapsedSeconds_, GetPreviewTimelineDuration(), uint32_t(std::lround(previewElapsedSeconds_ * 60)));
            const auto* selected = FindEmitter(selection_.emitterId);
            const std::string selection = selected ? selected->name : document_.GetDisplayName();
            std::string flags;
            if (!soloEmitterId_.empty()) flags += "  | " + TEXT::Localize("ui.vfx.hud.solo");
            if (timelineEdit_.autoKey) flags += "  | " + TEXT::Localize("ui.vfx.view.autoKey");
            if (previewBypassAdjustments_) flags += "  | " + TEXT::Localize("ui.vfx.hud.bypass");
            const auto& selectionLine = selection;
            const float hudWidth = (std::min)(canvasSize.x - 20,
                (std::max)({200.0f, ImGui::CalcTextSize(timing).x + 16,
                    ImGui::CalcTextSize(selectionLine.c_str()).x + 16}));
            const float flagsHeight = flags.empty() ? 0 :
                ImGui::CalcTextSize(flags.c_str(), nullptr, false, hudWidth - 16).y + ImGui::GetStyle().ItemSpacing.y;
            const float height = ImGui::GetTextLineHeightWithSpacing() * 2 + flagsHeight + 12;
            const ImVec2 corner{origin.x + 10, origin.y + canvasSize.y - height - 10};
            drawList->PushClipRect(corner, {corner.x + hudWidth, corner.y + height}, true);
            drawList->AddRectFilled(corner, {corner.x + hudWidth, corner.y + height}, IM_COL32(13,18,26,205), 5);
            drawList->AddText({corner.x+8,corner.y+5}, IM_COL32(218,230,242,255), timing);
            drawList->AddText({corner.x+8,corner.y+5+ImGui::GetTextLineHeightWithSpacing()}, IM_COL32(110,200,255,255), selectionLine.c_str());
            if (!flags.empty()) drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                {corner.x+8,corner.y+5+2*ImGui::GetTextLineHeightWithSpacing()}, IM_COL32(255,205,112,255),
                flags.c_str(), nullptr, hudWidth - 16);
            drawList->PopClipRect();
        }
        if (!gizmoInteracting && !pickedLayer.empty() && ImGui::IsMouseClicked(0))
            Select({VfxWorkspaceSelectionKind::Emitter, pickedLayer, pickedLayer});
        if (hovered && !gizmoInteracting) {
            const ImGuiIO& io = ImGui::GetIO();
            EditorDirectorCameraInput input{};
            input.orbitActive =
                io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            input.panActive =
                ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            input.lookActive =
                ImGui::IsMouseDown(ImGuiMouseButton_Right);
            input.mouseDeltaX = io.MouseDelta.x;
            input.mouseDeltaY = io.MouseDelta.y;
            input.wheelDelta = io.MouseWheel;
            const float aspect = canvasSize.y > 0.0f
                ? canvasSize.x / canvasSize.y
                : 1.0f;
            if (cameraController_.Update(
                    input, deltaSeconds, aspect)) {
                ++cameraRevision_;
                if (cameraRevision_ == 0u) {
                    cameraRevision_ = 1u;
                }
            }
        }

        SubmitPreviewRequest(previewView, canvasVisible);
        ImGui::SetCursorScreenPos({ origin.x, origin.y + canvasSize.y });
        ImGui::Dummy({ 1.0f, 1.0f });
        ImGui::End();
#else
        (void)previewView;
        (void)deltaSeconds;
#endif
    }

} // namespace HIKARI::EDITOR
