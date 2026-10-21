#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

#include <cmath>
#include <json.hpp>

#include "imgui.h"

namespace HIKARI {
namespace {
// 操縦の取り消しの束ね番号の上位（他の束ねと重ならない値）。
constexpr uint64_t kCameraPilotMergeGroupBase = 0xC0DE000000000000ull;

const GameObject* SelectedCamera(const EditorContext& context) {
    const GameObject* selected = context.selection.selectedObject;
    return selected != nullptr && context.selection.GetSelectedObjectCount() == 1u &&
                   selected->GetComponent<CameraComponent>() != nullptr
               ? selected
               : nullptr;
}

bool NearlyEqual(const MATH::Vec3& a, const MATH::Vec3& b) {
    return std::abs(a.x - b.x) < 1.0e-4f && std::abs(a.y - b.y) < 1.0e-4f && std::abs(a.z - b.z) < 1.0e-4f;
}

// 注視点までの距離はカメラごとに違うので、向きは長さ 1 の前で比べる。
MATH::Vec3 Forward(const Camera3D& camera) { return MATH::Normalize(camera.GetTarget() - camera.GetPosition()); }
} // namespace

void DocumentSceneEditorController::DrawCameraPilotButtons(DocumentSceneBase& scene) {
    const GameObject* camera = SelectedCamera(context_);
    if (camera == nullptr) {
        return;
    }
    const ImVec2 buttonSize{26.0f, 26.0f};
    ImGui::SameLine(0.0f, 10.0f);
    const bool piloting = cameraPilot_.Active();
    if (EDITOR::IconToggleButton(EDITOR::EditorGlyph::Camera, "ViewportCameraPilot", piloting, buttonSize,
                                 TEXT::Localize("cine.pilot").c_str())) {
        if (piloting) {
            EndCameraPilot(scene);
        } else {
            BeginCameraPilot(scene, camera->GetDocumentId());
        }
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(piloting);
    if (EDITOR::IconButton(EDITOR::EditorGlyph::Focus, "ViewportCameraAlign", EDITOR::EditorButtonTone::Quiet, buttonSize,
                           TEXT::Localize("cine.alignCamera").c_str())) {
        AlignCameraToView(scene, camera->GetDocumentId(), 0);
    }
    ImGui::EndDisabled();
}

void DocumentSceneEditorController::BeginCameraPilot(DocumentSceneBase& scene, SceneObjectId camera) {
    Camera3D& editorCamera = scene.GetCamera();
    Camera3D view;
    if (!scene.TryResolveCameraObjectView(camera, editorCamera.GetAspect(), view)) {
        return;
    }
    cameraPilot_ = {camera, kCameraPilotMergeGroupBase | ++cameraPilotSessions_, editorCamera.GetFovYRad()};
    // 視口の画のカメラもこのフレームから機位の視点にする（カメラ操作は次のフレームから効く）。
    scene.GetDebugCamera().ResetFromCamera(view);
    editorCamera.SetLookAt(view.GetPosition(), view.GetTarget());
    editorCamera.SetPerspective(view.GetFovYRad(), editorCamera.GetAspect(), editorCamera.GetNearZ(),
                                editorCamera.GetFarZ());
    // 入っただけでは機位を書かない（動かした時だけ書く）。
    cameraPilot_.lastPosition = editorCamera.GetPosition();
    cameraPilot_.lastForward = Forward(editorCamera);
}

void DocumentSceneEditorController::EndCameraPilot(DocumentSceneBase& scene) {
    if (!cameraPilot_.Active()) {
        return;
    }
    Camera3D& editorCamera = scene.GetCamera();
    editorCamera.SetPerspective(cameraPilot_.editorFovY, editorCamera.GetAspect(), editorCamera.GetNearZ(),
                                editorCamera.GetFarZ());
    cameraPilot_ = {};
}

void DocumentSceneEditorController::UpdateCameraPilot(DocumentSceneBase& scene, bool playRunning) {
    if (!cameraPilot_.Active()) {
        return;
    }
    const GameObject* camera = SelectedCamera(context_);
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput;
    if (playRunning || camera == nullptr || camera->GetDocumentId() != cameraPilot_.camera || escape) {
        EndCameraPilot(scene);
        return;
    }
    // 視点が動いた時だけ書く（止まっている間は取り消しの履歴も増やさない）。
    const Camera3D& editorCamera = scene.GetCamera();
    if (NearlyEqual(editorCamera.GetPosition(), cameraPilot_.lastPosition) &&
        NearlyEqual(Forward(editorCamera), cameraPilot_.lastForward)) {
        return;
    }
    AlignCameraToView(scene, cameraPilot_.camera, cameraPilot_.mergeGroup);
    cameraPilot_.lastPosition = editorCamera.GetPosition();
    cameraPilot_.lastForward = Forward(editorCamera);
}

void DocumentSceneEditorController::AlignCameraToView(DocumentSceneBase& scene, SceneObjectId camera,
                                                      uint64_t mergeGroup) {
    const GameObject* object = scene.GetWorld().FindObject(camera);
    if (object == nullptr) {
        return;
    }
    // 視点の姿勢（傾きなし）を機位の親の空間へ移す（scene.set_transform は親から見た姿勢を書く）。
    const EDITOR::DirectorCameraPose pose = EDITOR::MakeDirectorCameraPose(scene.GetCamera());
    MATH::Mat4 matrix = MATH::Mat4::TRS(pose.position, pose.rotation, {1.0f, 1.0f, 1.0f});
    if (object->GetParent() != nullptr) {
        matrix = MATH::Inverse(object->GetParent()->GetTransform().GetWorldMatrix()) * matrix;
    }
    MATH::Vec3 position{}, scale{};
    MATH::Quat rotation{};
    if (!MATH::DecomposeTRS(matrix, position, rotation, scale)) {
        return;
    }
    const MATH::Vec3 euler = MATH::EulerXYZDegreesFromQuat(rotation);
    nlohmann::json parameters{{"objectId", camera.value},
                              {"position", {position.x, position.y, position.z}},
                              {"rotation", {euler.x, euler.y, euler.z}}};
    if (mergeGroup != 0u) {
        parameters["mergeGroup"] = mergeGroup;
    }
    (void)operationRegistry_.Execute("scene.set_transform", parameters);
}

} // namespace HIKARI
