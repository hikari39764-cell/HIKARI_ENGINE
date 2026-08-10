#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cstdint>

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void VfxWorkspaceController::DrawPreviewWindow(
        EditorViewInstance& previewView,
        float deltaSeconds) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Preview###Vfx/Preview")) {
            RENDER3D::EDITORVIEW::ClearRequest(
                previewView.renderViewId);
            ImGui::End();
            return;
        }

        if (ImGui::Button("Perspective")) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Perspective);
            cameraController_.Focus({ 0.0f, 0.75f, 0.0f }, 5.0f);
            ++cameraRevision_;
            cameraCutPending_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Front")) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Front);
            cameraController_.Focus({ 0.0f, 0.75f, 0.0f }, 5.0f);
            ++cameraRevision_;
            cameraCutPending_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Top")) {
            cameraController_.ApplyViewPreset(
                EditorDirectorCameraViewPreset::Top);
            cameraController_.Focus({ 0.0f, 0.0f, 0.0f }, 5.0f);
            ++cameraRevision_;
            cameraCutPending_ = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &showPreviewGrid_);
        ImGui::SameLine();
        ImGui::Checkbox("Axes", &showPreviewAxes_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0f);
        float playbackSpeed = previewPlaybackSpeed_;
        if (ImGui::DragFloat(
                "##VfxPreviewSpeed",
                &playbackSpeed,
                0.05f,
                0.1f,
                4.0f,
                "%.2fx")) {
            SetPreviewPlaybackSpeed(playbackSpeed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Preview playback speed");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("|>")) {
            (void)StepPreviewFrame(1, statusMessage_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step 1 frame (pauses preview)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("|>>")) {
            (void)StepPreviewFrame(10, statusMessage_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step 10 frames (pauses preview)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Focus")) {
            (void)FocusSelectedEmitter(statusMessage_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Focus camera on selected emitter");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%zu emitters",
            previewRenderInput_ != nullptr
                ? previewRenderInput_->emitters.size()
                : 0u);
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
        if (hovered) {
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
