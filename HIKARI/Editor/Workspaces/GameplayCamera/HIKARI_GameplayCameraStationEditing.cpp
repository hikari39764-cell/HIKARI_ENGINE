#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "imgui.h"
#include <numbers>

namespace HIKARI::EDITOR {
namespace {
nlohmann::json Vector(const MATH::Vec3& value) { return {value.x, value.y, value.z}; }
} // namespace
void GameplayCameraWorkspaceController::DrawStationInspector(DocumentSceneBase& scene,
                                                             EditorOperationRegistry& operations) {
    const auto* object = scene.GetWorld().FindObject(camera_);
    if (!object)
        return;
    const auto* camera = object->GetComponent<CameraComponent>();
    if (!camera)
        return;
    ImGui::TextWrapped("共享场景机位。引用它的演出端点会一起更新；修改保存到场景。");
    auto position = object->GetTransform().position;
    auto rotation = MATH::EulerXYZDegreesFromQuat(object->GetTransform().rotation);
    ImGui::BeginDisabled(scene.IsRuntimePlayActive());
    bool changed = ImGui::DragFloat3("位置", &position.x, .01f);
    changed |= ImGui::DragFloat3("旋转", &rotation.x, .1f);
    if (changed)
        message_ =
            operations
                .Execute("scene.set_transform",
                         {{"objectId", camera_.value}, {"position", Vector(position)}, {"rotation", Vector(rotation)}})
                .message;
    Camera3D view;
    if (scene.TryResolveCameraObjectView(camera_, 16.f / 9, view)) {
        float fov = view.GetFovYRad() * 180.f / std::numbers::pi_v<float>;
        if (ImGui::DragFloat("视野 FOV", &fov, .1f, 1, 179)) {
            // 変更コマンドは文書を再構成し得るため、走査中に実行しない。
            uint64_t componentId = 0;
            for (const auto& authored : scene.GetSceneDocument().objects)
                if (authored.id == camera_)
                    for (const auto& component : authored.components)
                        if (component.type == "CameraComponent")
                            componentId = component.id.value;
            if (componentId)
                message_ = operations
                               .Execute("component.property.set", {{"objectId", camera_.value},
                                                                   {"componentId", componentId},
                                                                   {"propertyId", "verticalFovDegrees"},
                                                                   {"value", fov}})
                               .message;
        }
    }
    if (ImGui::Button("保存场景机位"))
        message_ = operations.Execute("scene.save", {}).message;
    ImGui::EndDisabled();
}
void GameplayCameraWorkspaceController::ApplyStationViewEdit(DocumentSceneBase& scene,
                                                             EditorOperationRegistry& operations,
                                                             const DirectorViewPanelResult& result) {
    const auto* object = scene.GetWorld().FindObject(camera_);
    if (!object)
        return;
    MATH::Vec3 position, scale;
    MATH::Quat rotation;
    if (result.alignCameraRequested || result.pilotPoseChanged) {
        const auto& pose = result.pilotPoseChanged ? result.pilotPose : result.alignPose;
        auto matrix = MATH::Mat4::TRS(pose.position, pose.rotation, {1, 1, 1});
        if (object->GetParent())
            matrix = MATH::Inverse(object->GetParent()->GetTransform().GetWorldMatrix()) * matrix;
        if (!MATH::DecomposeTRS(matrix, position, rotation, scale))
            return;
    } else if (result.gizmo.changed) {
        position = result.gizmo.transform.position;
        rotation = result.gizmo.rotation;
    } else
        return;
    // 直接 Runtime を書かず、数値入力・操縦・ギズモを共通の Scene 履歴に積む。
    message_ = operations
                   .Execute("scene.set_transform", {{"objectId", camera_.value},
                                                    {"position", Vector(position)},
                                                    {"rotation", Vector(MATH::EulerXYZDegreesFromQuat(rotation))}})
                   .message;
}
} // namespace HIKARI::EDITOR
