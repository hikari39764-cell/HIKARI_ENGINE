#include "HIKARI_DebugCameraPanel.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void DebugCameraPanel::Draw(DebugCameraController3D& debugCamera) const {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.debugCamera", "Debug Camera").c_str())) {
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
    if (ImGui::Checkbox(TEXT::Localize("ui.debugCamera.enabled").c_str(), &enabled)) {
        debugCamera.SetEnabled(enabled);
    }

    MATH::Vec3 position = debugCamera.GetPosition();
    if (ImGui::DragFloat3(TEXT::Localize("ui.debugCamera.position").c_str(), &position.x, 0.02f)) {
        debugCamera.SetPosition(position);
    }

    float yaw = debugCamera.GetYaw();
    if (ImGui::DragFloat(TEXT::Localize("ui.debugCamera.yaw").c_str(), &yaw, 0.01f)) {
        debugCamera.SetYaw(yaw);
    }

    float pitch = debugCamera.GetPitch();
    if (ImGui::DragFloat(TEXT::Localize("ui.debugCamera.pitch").c_str(), &pitch, 0.01f)) {
        debugCamera.SetPitch(pitch);
    }

    DebugCamera3DSettings& settings = debugCamera.Settings();
    ImGui::DragFloat(TEXT::Localize("ui.debugCamera.moveSpeed").c_str(), &settings.moveSpeed, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat(TEXT::Localize("ui.debugCamera.fastMultiplier").c_str(), &settings.fastMultiplier, 0.05f, 1.0f,
                     20.0f);
    ImGui::DragFloat(TEXT::Localize("ui.debugCamera.mouseLookSensitivity").c_str(), &settings.mouseLookSensitivity,
                     0.0001f, 0.0001f, 0.03f, "%.4f");
    ImGui::DragFloat(TEXT::Localize("ui.debugCamera.wheelMoveStep").c_str(), &settings.wheelMoveStep, 0.01f, 0.01f,
                     10.0f);

    if (ImGui::Button(TEXT::Localize("ui.common.reset").c_str())) {
        const MATH::Vec3 resetPos{0.0f, 2.0f, -6.0f};
        debugCamera.Reset(resetPos, 0.0f, 0.0f);
    }
#else
    (void)debugCamera;
#endif
}

} // namespace HIKARI
