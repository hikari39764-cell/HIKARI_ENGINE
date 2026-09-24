#include "Render2D/HIKARI_Camera.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Input/Runtime/HIKARI_InputActionIds.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::CAMERA {
namespace {

struct State {
    Vector2 position{0.0f, 0.0f}; // 注視するワールド座標
    Vector2 scale{1.0f, 1.0f};    // ズーム
    float rotation = 0.0f;        // ラジアン
    Vector2 pivot{0.0f, 0.0f};    // 回転／拡大の画面上の基準点
    float pitch = 1.0f;           // Y 方向の潰し（俯仰）
};

constexpr float kDebugZoomStep = 0.02f;
constexpr float kDebugZoomMin = 0.05f;
constexpr float kDebugZoomMax = 12.0f;

State gState{};
Vector2 gScreenCenter{0.0f, 0.0f};
int gScreenW = 1920;
int gScreenH = 1080;
Matrix3x3 gView = Matrix3x3::MakeIdentity();
bool gDebugControlEnabled = true;

void ApplyDebugControl(const INPUT::InputSnapshot* input) {
    if (!gDebugControlEnabled || input == nullptr) {
        return;
    }
    // 画面上のドラッグ量を現在の回転・俯仰でワールド移動へ戻す
    if (input->IsDown(INPUT::ActionIds::EditorCameraPanHeld)) {
        const std::array<float, 2> pan = input->GetAxis2D(INPUT::ActionIds::EditorCameraPan);
        if (std::fabs(pan[0]) > 0.0001f || std::fabs(pan[1]) > 0.0001f) {
            const float c = std::cos(gState.rotation);
            const float s = std::sin(gState.rotation);
            const float worldMoveX = pan[0] * c - pan[1] * s;
            const float worldMoveY = (pan[0] * s + pan[1] * c) / (std::max)(gState.pitch, 0.1f);
            gState.position.x -= worldMoveX / gState.scale.x;
            gState.position.y -= worldMoveY / gState.scale.y;
        }
    }

    const float zoomInput = input->GetAxis1D(INPUT::ActionIds::EditorCameraZoom);
    if (std::fabs(zoomInput) > 0.0001f) {
        const float factor = (std::max)(1.0f + zoomInput * kDebugZoomStep, 0.1f);
        gState.scale.x = std::clamp(gState.scale.x * factor, kDebugZoomMin, kDebugZoomMax);
        gState.scale.y = std::clamp(gState.scale.y * factor, kDebugZoomMin, kDebugZoomMax);
    }

    if (input->IsDown(INPUT::ActionIds::EditorCameraLookHeld)) {
        const std::array<float, 2> look = input->GetAxis2D(INPUT::ActionIds::EditorCameraLook);
        if (std::fabs(look[0]) > 0.0001f || std::fabs(look[1]) > 0.0001f) {
            gState.rotation += -look[0] * 0.003f;
            gState.pitch = std::clamp(gState.pitch - look[1] * 0.003f, 0.2f, 1.0f);
        }
    }
}

} // namespace

void SetScreenCenter(const Vector2& screenCenter) { gScreenCenter = screenCenter; }

void SetScreenSize(int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    gScreenW = w;
    gScreenH = h;
    gScreenCenter = {w * 0.5f, h * 0.5f};
    gState.pivot = gScreenCenter;
}

int GetScreenWidth() { return gScreenW; }
int GetScreenHeight() { return gScreenH; }
void EnableDebugControl(bool enable) { gDebugControlEnabled = enable; }

void Update(float /*deltaTime*/, const INPUT::InputSnapshot* input) {
    ApplyDebugControl(input);
    // 位置→回転→俯仰→ズーム→画面中心
    gView = Matrix3x3::MakeTranslate(-gState.position.x, -gState.position.y) * Matrix3x3::MakeRotate(gState.rotation) *
            Matrix3x3::MakeScale(1.0f, gState.pitch) * Matrix3x3::MakeScale(gState.scale.x, gState.scale.y) *
            Matrix3x3::MakeTranslate(gScreenCenter.x, gScreenCenter.y);
}

const Matrix3x3& GetViewMatrix() { return gView; }

} // namespace HIKARI::CAMERA
