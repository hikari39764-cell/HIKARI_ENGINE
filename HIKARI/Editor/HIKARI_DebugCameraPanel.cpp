#include "HIKARI_DebugCameraPanel.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DebugCameraPanel::Draw(DebugCameraController3D& debugCamera) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Debug Camera")) {
            ImGui::End();
            return;
        }

        DrawContents(debugCamera);

        ImGui::End();
#else
        (void)debugCamera;
#endif
    }

    void DebugCameraPanel::DrawContents(DebugCameraController3D& debugCamera) const {
#if defined(HIKARI_WITH_EDITOR)
        bool enabled = debugCamera.IsEnabled();
        if (ImGui::Checkbox("Enabled", &enabled)) {
            debugCamera.SetEnabled(enabled);
        }

        MATH::Vec3 position = debugCamera.GetPosition();
        if (ImGui::DragFloat3("Position", &position.x, 0.02f)) {
            debugCamera.SetPosition(position);
        }

        float yaw = debugCamera.GetYaw();
        if (ImGui::DragFloat("Yaw", &yaw, 0.01f)) {
            debugCamera.SetYaw(yaw);
        }

        float pitch = debugCamera.GetPitch();
        if (ImGui::DragFloat("Pitch", &pitch, 0.01f)) {
            debugCamera.SetPitch(pitch);
        }

        DebugCamera3DSettings& settings = debugCamera.Settings();
        ImGui::DragFloat("Move Speed", &settings.moveSpeed, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat("Fast Multiplier", &settings.fastMultiplier, 0.05f, 1.0f, 20.0f);
        ImGui::DragFloat("Mouse Look Sensitivity", &settings.mouseLookSensitivity, 0.0001f, 0.0001f, 0.03f, "%.4f");
        ImGui::DragFloat("Wheel Move Step", &settings.wheelMoveStep, 0.01f, 0.01f, 10.0f);

        if (ImGui::Button("Reset")) {
            const MATH::Vec3 resetPos{ 0.0f, 2.0f, -6.0f };
            debugCamera.Reset(resetPos, 0.0f, 0.0f);
        }
#else
        (void)debugCamera;
#endif
    }

} // namespace HIKARI
