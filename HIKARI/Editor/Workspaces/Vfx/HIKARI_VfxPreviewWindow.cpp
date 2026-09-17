#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Core/Text/HIKARI_Localization.h"

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

        if (ImGui::Button(VfxUiText("View").c_str())) ImGui::OpenPopup("VfxView");
        if (ImGui::BeginPopup("VfxView")) {
        if (ImGui::Button(VfxUiText("Perspective").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Perspective);
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Front").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Front);
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Top").c_str())) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Top);
        }
        ImGui::SameLine();
        ImGui::Checkbox(VfxUiText("Grid").c_str(), &showPreviewGrid_);
        ImGui::SameLine();
        ImGui::Checkbox(VfxUiText("Axes").c_str(), &showPreviewAxes_);
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.path").c_str(), &timelineEdit_.showMotionPath);
        ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Lighting").c_str())) ImGui::OpenPopup("VfxPreviewLighting");
        if (ImGui::BeginPopup("VfxPreviewLighting")) {
            ImGui::TextUnformatted(VfxUiText("Preview lighting (not saved in the effect)").c_str());
            bool changed = ImGui::SliderFloat(VfxUiText("Direction").c_str(), &previewLightYaw_, -180.0f, 180.0f, "%.0f deg");
            changed |= ImGui::SliderFloat(VfxUiText("Brightness").c_str(), &previewLightIntensity_, 0.0f, 5.0f, "%.2f");
            if (changed && !previewLighting_->lights.empty()) {
                const float yaw = previewLightYaw_ * 0.01745329252f;
                auto& key = previewLighting_->lights.front();
                key.direction = {std::sin(yaw)*0.6f, -0.8f, std::cos(yaw)*0.6f};
                key.intensity = previewLightIntensity_;
            }
            ImGui::TextWrapped(VfxUiText("StandardLit and CustomLit respond to this light. Unlit effects keep their authored color.").c_str());
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(VfxUiText("Focus").c_str())) {
            (void)FocusSelectedEmitter(statusMessage_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Focus camera on selected emitter");
        }
        ImGui::SameLine();
        const char* operations[] = {"Move", "Rotate", "Scale"};
        int operation = static_cast<int>(layerGizmoState_.operation);
        ImGui::SetNextItemWidth(90);
        if (ImGui::BeginCombo("##LayerTransform", VfxUiText(operations[operation]).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(VfxUiText(operations[i]).c_str(), operation == i))
                    layerGizmoState_.operation = static_cast<EditorTransformGizmoOperation>(i);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(layerGizmoState_.mode == EditorTransformGizmoMode::World ? VfxUiText("World (X)").c_str() : VfxUiText("Local (X)").c_str()))
            layerGizmoState_.mode = layerGizmoState_.mode == EditorTransformGizmoMode::World ? EditorTransformGizmoMode::Local : EditorTransformGizmoMode::World;
        ImGui::SameLine();
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.snap").c_str(), &layerGizmoState_.snapEnabled);
        ImGui::SameLine();
        ImGui::Checkbox(TEXT::Localize("ui.vfx.view.autoKey").c_str(), &timelineEdit_.autoKey);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TEXT::Localize("ui.vfx.view.editModeHelp").c_str());
        ImGui::TextDisabled(
            VfxUiText("%zu visible layers").c_str(),
            static_cast<size_t>(std::count_if(document_.Asset().emitters.begin(),
                document_.Asset().emitters.end(), [this](const auto& emitter) {
                    return emitter.basic.enabled && IsEmitterPreviewVisible(emitter.id);
                })));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Alt+LMB orbit\nMMB pan\nRMB look\nMouse wheel dolly");
        }

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
            if (hovered && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) (void)FocusSelectedEmitter(statusMessage_);
        }
        std::string pickedLayer;
        bool gizmoInteracting = DrawLayerHandles(
            {origin.x, origin.y, canvasSize.x, canvasSize.y}, hovered, layerGizmoEditing_, pickedLayer);
        if (!gizmoInteracting)
            gizmoInteracting = DrawLayerGizmo({origin.x, origin.y, canvasSize.x, canvasSize.y});
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
