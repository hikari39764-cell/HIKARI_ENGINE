#include "HIKARI_EditorViewportInput.h"

#include <cmath>

#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

namespace {
struct GameViewportInputState {
    bool hasRect = false;
    bool windowFocused = false;
    bool mouseCaptured = false;
    bool gizmoCaptured = false;
    bool playInputEnabled = false;
    uint32_t windowId = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

GameViewportInputState gGameViewportInput{};

#if defined(HIKARI_WITH_EDITOR)
bool HasImGuiContext() { return ImGui::GetCurrentContext() != nullptr; }

bool IsMouseInsideGameViewport() {
    if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
        return false;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= gGameViewportInput.x && mouse.y >= gGameViewportInput.y &&
           mouse.x < gGameViewportInput.x + gGameViewportInput.width &&
           mouse.y < gGameViewportInput.y + gGameViewportInput.height;
}

bool IsCameraMouseButtonDown() {
    return ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
}

bool IsCameraMouseButtonClicked() {
    return ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
}

CameraControlCaptureState QueryCameraControlCaptureState() {
    CameraControlCaptureState state{};
    if (!HasImGuiContext()) {
        return state;
    }

    const ImGuiIO& io = ImGui::GetIO();
    state.wantMouse = io.WantCaptureMouse;
    state.wantKeyboard = io.WantCaptureKeyboard;
    state.acceptWheel = !state.wantMouse;
    if (HasGameViewportInputRect()) {
        state.wantMouse = !IsGameViewportMouseInputActive();
        state.wantKeyboard = !IsGameViewportKeyboardInputActive();
        state.acceptWheel = IsGameViewportWheelInputActive();
    }
    return state;
}

struct CameraControlCaptureRegistration {
    CameraControlCaptureRegistration() { SetCameraControlCaptureQuery(&QueryCameraControlCaptureState); }
};

CameraControlCaptureRegistration gCameraControlCaptureRegistration{};
#endif
} // namespace

void SetGameViewportInputRect(float x, float y, float width, float height, bool windowFocused) {
#if defined(HIKARI_WITH_EDITOR)
    if (width <= 1.0f || height <= 1.0f) {
        ClearGameViewportInputRect();
        return;
    }

    gGameViewportInput.hasRect = true;
    gGameViewportInput.windowFocused = windowFocused;
    gGameViewportInput.windowId =
        HasImGuiContext() && ImGui::GetCurrentContext()->CurrentWindow ? ImGui::GetCurrentWindow()->ID : 0u;
    gGameViewportInput.x = x;
    gGameViewportInput.y = y;
    gGameViewportInput.width = width;
    gGameViewportInput.height = height;
#else
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)windowFocused;
#endif
}

void ClearGameViewportInputRect() { gGameViewportInput = {}; }

void SetGameViewportGizmoCapture(bool captured) {
#if defined(HIKARI_WITH_EDITOR)
    gGameViewportInput.gizmoCaptured = captured;
#else
    (void)captured;
#endif
}

bool HasGameViewportInputRect() { return gGameViewportInput.hasRect; }

void SetGameViewportPlayInputEnabled(bool enabled) { gGameViewportInput.playInputEnabled = enabled; }

bool IsGameViewportPlayInputActive() noexcept {
#if defined(HIKARI_WITH_EDITOR)
    if (!gGameViewportInput.playInputEnabled) {
        return false;
    }
    const ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiWindow* window =
        context && gGameViewportInput.windowId ? ImGui::FindWindowByID(gGameViewportInput.windowId) : nullptr;
    const bool ownsWindows = window && context->NavWindow && context->HoveredWindow &&
                             ImGui::IsWindowChildOf(context->NavWindow, window, false, false) &&
                             ImGui::IsWindowChildOf(context->HoveredWindow, window, false, false);
    const bool editorItemActive = context && context->ActiveId != 0u &&
                                  !(window && context->ActiveIdWindow == window && context->ActiveId == window->MoveId);
    const bool active = ownsWindows && context->WithinFrameScope && !gGameViewportInput.gizmoCaptured &&
                        !context->IO.AppFocusLost && !context->IO.WantTextInput && !editorItemActive &&
                        !QueryEditorViewInputBlockState().pointer && IsMouseInsideGameViewport() &&
                        !ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (!active) {
        gGameViewportInput.playInputEnabled = false;
    }
    return active;
#else
    return false;
#endif
}

bool TryGetGameViewportPointer(INPUT::InputPointerState& pointer, uint32_t& width, uint32_t& height) noexcept {
    pointer = {};
    width = height = 0u;
#if defined(HIKARI_WITH_EDITOR)
    if (!IsGameViewportPlayInputActive()) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.AppFocusLost || !std::isfinite(gGameViewportInput.width) || !std::isfinite(gGameViewportInput.height)) {
        return false;
    }
    width = static_cast<uint32_t>(std::lround(gGameViewportInput.width));
    height = static_cast<uint32_t>(std::lround(gGameViewportInput.height));
    pointer.normalizedX = (io.MousePos.x - gGameViewportInput.x) / gGameViewportInput.width;
    pointer.normalizedY = (io.MousePos.y - gGameViewportInput.y) / gGameViewportInput.height;
    pointer.x = pointer.normalizedX * static_cast<float>(width);
    pointer.y = pointer.normalizedY * static_cast<float>(height);
    pointer.primaryDown = io.MouseDown[ImGuiMouseButton_Left];
    pointer.secondaryDown = io.MouseDown[ImGuiMouseButton_Right];
    return width > 0u && height > 0u;
#else
    return false;
#endif
}

bool IsGameViewportMouseInputActive() {
#if defined(HIKARI_WITH_EDITOR)
    if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
        return false;
    }
    if (gGameViewportInput.gizmoCaptured || QueryEditorViewInputBlockState().pointer) {
        gGameViewportInput.mouseCaptured = false;
        return false;
    }

    const bool hovered = IsMouseInsideGameViewport();
    if (!IsCameraMouseButtonDown()) {
        gGameViewportInput.mouseCaptured = false;
    } else if (hovered && IsCameraMouseButtonClicked()) {
        gGameViewportInput.mouseCaptured = true;
    }

    if (IsCameraMouseButtonDown()) {
        return gGameViewportInput.mouseCaptured;
    }

    return hovered;
#else
    return false;
#endif
}

bool IsGameViewportWheelInputActive() {
#if defined(HIKARI_WITH_EDITOR)
    if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
        return false;
    }
    const EditorViewInputBlockState block = QueryEditorViewInputBlockState();
    if (gGameViewportInput.gizmoCaptured || block.pointer || block.keyboard) {
        return false;
    }

    return IsMouseInsideGameViewport();
#else
    return false;
#endif
}

bool IsGameViewportKeyboardInputActive() {
#if defined(HIKARI_WITH_EDITOR)
    if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
        return false;
    }
    const EditorViewInputBlockState block = QueryEditorViewInputBlockState();
    if (gGameViewportInput.gizmoCaptured || block.keyboard) {
        return false;
    }

    return gGameViewportInput.mouseCaptured || (gGameViewportInput.windowFocused && IsMouseInsideGameViewport());
#else
    return false;
#endif
}

} // namespace HIKARI::EDITOR
