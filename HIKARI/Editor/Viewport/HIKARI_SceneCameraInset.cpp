#include "Editor/Viewport/HIKARI_SceneCameraInset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
// 游戏摄像机工作区の相机画面と同じ 16:9。主视口が狭い時は出さない。
constexpr float kAspect = 16.0f / 9.0f;
constexpr float kMargin = 12.0f;
constexpr float kMinimumViewportWidth = 480.0f;
constexpr float kMinimumViewportHeight = 300.0f;
// 小窓は主ビューの自動露出を借りる。順応の途中（起動直後や昼夜を変えた直後）に一度だけ描くと白や黒に飛んだまま
// 残るので、露出がこの段数以上動いたら描き直す。
constexpr float kExposureRedrawStops = 0.125f;
} // namespace

SceneViewportRect SceneCameraInset::Layout(const DocumentSceneBase& scene, SceneObjectId camera,
                                           const SceneViewportRect& viewport) {
    camera_ = {};
    rect_ = {};
    if (camera.value == 0 || viewport.width < kMinimumViewportWidth || viewport.height < kMinimumViewportHeight ||
        !scene.TryResolveCameraObjectView(camera, kAspect, view_))
        return rect_;
    const float width = std::clamp(viewport.width * 0.28f, 200.0f, 420.0f);
    const float height = width / kAspect;
    rect_ = {viewport.x + viewport.width - width - kMargin, viewport.y + viewport.height - height - kMargin, width,
             height};
    camera_ = camera;
    return rect_;
}

void SceneCameraInset::Draw(const DocumentSceneBase& scene, ImDrawList& draw) {
    namespace VIEW = RENDER3D::EDITORVIEW;
    if (camera_.value == 0) {
        if (submitted_)
            VIEW::ClearRequest(kSceneCameraInsetRenderViewId);
        submitted_ = false;
        return;
    }
    VIEW::EditorInteractiveViewRequest request;
    request.viewId = kSceneCameraInsetRenderViewId;
    request.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    request.width = static_cast<uint32_t>(std::lround(rect_.width));
    request.height = static_cast<uint32_t>(std::lround(rect_.height));
    request.cameraFrame.camera = view_;
    request.cameraFrame.valid = true;
    // 変わった時だけ一枚ずつ描くので、毎回前の画の履歴を使わない（検査ビューの撮影と同じ）。
    request.cameraFrame.cameraCut = true;
    // 機位の姿勢は文書から来るので、文書の版と機位で描き直しを決める。
    request.cameraFrame.sourceCameraObjectId = camera_.value;
    request.cameraFrame.revision = scene.GetSceneDocumentRevision();
    request.sceneRevision = scene.GetSceneDocumentRevision();
    const float exposureLog2 = std::log2((std::max)(POST::PostSystem::GetExposureState().exposure, 1.0e-30f));
    if (std::abs(exposureLog2 - exposureLog2_) > kExposureRedrawStops) {
        exposureLog2_ = exposureLog2;
        ++exposureRevision_;
    }
    request.contentRevision = exposureRevision_;
    request.visible = true;
    request.drawDebug = false;
    VIEW::SubmitRequest(request);
    submitted_ = true;

    const ImVec2 min{rect_.x, rect_.y};
    const ImVec2 max{rect_.x + rect_.width, rect_.y + rect_.height};
    draw.AddRectFilled(min, max, IM_COL32(8, 10, 13, 235), 3.0f);
    const VIEW::EditorInteractiveViewOutput output = VIEW::GetOutput(kSceneCameraInsetRenderViewId);
    if (output.ready && output.colorSrv.ptr != 0)
        draw.AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(output.colorSrv.ptr)), min, max);
    draw.AddRect(min, max, IM_COL32(70, 210, 225, 220), 3.0f, 0, 1.5f);
    if (const GameObject* object = scene.GetWorld().FindObject(camera_)) {
        const char* name = object->GetName().c_str();
        const ImVec2 size = ImGui::CalcTextSize(name);
        draw.AddRectFilled(min, {min.x + size.x + 12.0f, min.y + size.y + 6.0f}, IM_COL32(8, 12, 17, 200), 3.0f);
        draw.AddText({min.x + 6.0f, min.y + 3.0f}, IM_COL32(220, 235, 240, 245), name);
    }
}

} // namespace HIKARI::EDITOR
