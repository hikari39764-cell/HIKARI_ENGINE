#include "HIKARI_DebugCameraPanel.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Widgets/HIKARI_NumberField.h"
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

    using EDITOR::DragNumber;
    using EDITOR::DragNumbers;
    MATH::Vec3 position = debugCamera.GetPosition();
    if (DragNumbers(TEXT::Localize("ui.debugCamera.position").c_str(), &position.x, 3,
                    {.step = 0.02, .proportional = false})) {
        debugCamera.SetPosition(position);
    }

    float yaw = debugCamera.GetYaw();
    if (DragNumber(TEXT::Localize("ui.debugCamera.yaw").c_str(), yaw, {.step = 0.01, .proportional = false})) {
        debugCamera.SetYaw(yaw);
    }

    float pitch = debugCamera.GetPitch();
    if (DragNumber(TEXT::Localize("ui.debugCamera.pitch").c_str(), pitch, {.step = 0.01, .proportional = false})) {
        debugCamera.SetPitch(pitch);
    }

    static const DebugCamera3DSettings kDefaults{};
    DebugCamera3DSettings& settings = debugCamera.Settings();
    (void)DragNumber(TEXT::Localize("ui.debugCamera.moveSpeed").c_str(), settings.moveSpeed,
                     {.minimum = 0.1, .maximum = 2000.0, .step = 0.05}, &kDefaults.moveSpeed);
    (void)DragNumber(TEXT::Localize("ui.debugCamera.fastMultiplier").c_str(), settings.fastMultiplier,
                     {.minimum = 1.0, .maximum = 20.0, .step = 0.05}, &kDefaults.fastMultiplier);
    (void)DragNumber(TEXT::Localize("ui.debugCamera.mouseLookSensitivity").c_str(), settings.mouseLookSensitivity,
                     {.minimum = 0.0001, .maximum = 0.03, .step = 0.0001}, &kDefaults.mouseLookSensitivity);
    (void)DragNumber(TEXT::Localize("ui.debugCamera.wheelMoveStep").c_str(), settings.wheelMoveStep,
                     {.minimum = 0.01, .maximum = 10.0, .step = 0.01}, &kDefaults.wheelMoveStep);

    if (ImGui::Button(TEXT::Localize("ui.common.reset").c_str())) {
        const MATH::Vec3 resetPos{0.0f, 2.0f, -6.0f};
        debugCamera.Reset(resetPos, 0.0f, 0.0f);
    }
#else
    (void)debugCamera;
#endif
}

} // namespace HIKARI
