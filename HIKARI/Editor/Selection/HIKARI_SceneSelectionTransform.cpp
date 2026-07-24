#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
    namespace {
        bool IsCamera(const GameObject& object) {
            return object.GetComponent<CameraComponent>() != nullptr;
        }

        bool HasSelectedAncestor(
            const GameObject& object,
            const std::unordered_set<uint64_t>& selectedIds) {

            for (const GameObject* parent = object.GetParent();
                parent != nullptr;
                parent = parent->GetParent()) {
                if (selectedIds.contains(
                        parent->GetDocumentId().value)) {
                    return true;
                }
            }
            return false;
        }

        bool DecomposeWorldToLocal(
            const GameObject& object,
            const MATH::Mat4& worldMatrix,
            Transform3D& outTransform) {

            const MATH::Mat4 localMatrix = object.GetParent() != nullptr
                ? MATH::Inverse(
                    object.GetParent()->GetTransform().GetWorldMatrix()) *
                    worldMatrix
                : worldMatrix;
            MATH::Vec3 position{};
            MATH::Quat rotation{};
            MATH::Vec3 scale{};
            if (!MATH::DecomposeTRS(
                    localMatrix,
                    position,
                    rotation,
                    scale)) {
                return false;
            }
            outTransform = object.GetTransform();
            outTransform.position = position;
            outTransform.rotation = rotation;
            outTransform.scale = scale;
            outTransform.useExplicitMatrix = false;
            return true;
        }

        void SyncDocumentTransform(
            DocumentSceneBase& scene,
            const GameObject& object) {

            for (SceneObjectData& documentObject :
                scene.GetSceneDocument().objects) {
                if (documentObject.id != object.GetDocumentId()) {
                    continue;
                }
                const Transform3D& runtime = object.GetTransform();
                const MATH::Vec3 rotationEulerDeg =
                    MATH::EulerXYZDegreesFromQuatNearest(
                        runtime.rotation,
                        documentObject.transform.rotationEulerDeg);
                documentObject.transform.position = runtime.position;
                documentObject.transform.scale = runtime.scale;
                documentObject.transform.rotationEulerDeg =
                    rotationEulerDeg;
                if (IsCamera(object)) {
                    documentObject.transform.scale = {
                        1.0f,
                        1.0f,
                        1.0f
                    };
                }
                return;
            }
        }

        bool ApplyLocalTransform(
            GameObject& object,
            Transform3D transform) {

            if (IsCamera(object)) {
                transform.scale = { 1.0f, 1.0f, 1.0f };
            }
            return object.SetLocalTransform(transform);
        }

    }

    SceneSelectionTransformResult DrawSceneSelectionTransformGizmo(
        DocumentSceneBase& scene,
        EditorSelection& selection,
        const EditorTransformGizmo& gizmo,
        const EditorTransformGizmoState& state,
        const EditorViewportRect& viewportRect) {

        SceneSelectionTransformResult result{};
        GameObject* activeObject = selection.selectedObject;
        if (activeObject == nullptr ||
            IsObjectEditorLocked(
                scene.GetSceneDocument(),
                activeObject->GetDocumentId()) ||
            (IsCamera(*activeObject) &&
                state.operation ==
                    EditorTransformGizmoOperation::Scale)) {
            return result;
        }

        std::vector<GameObject*> selectedObjects =
            selection.ResolveSelectedObjects(scene.GetWorld());
        std::erase_if(
            selectedObjects,
            [&](const GameObject* object) {
                return object == nullptr ||
                    IsObjectEditorLocked(
                        scene.GetSceneDocument(),
                        object->GetDocumentId());
            });
        if (selectedObjects.size() <= 1u) {
            if (IsCamera(*activeObject)) {
                GameObject proxy{ "Camera Gizmo Proxy" };
                proxy.SetDocumentId(activeObject->GetDocumentId());
                Transform3D proxyTransform =
                    activeObject->GetTransform();
                proxyTransform.scale = { 1.0f, 1.0f, 1.0f };
                proxyTransform.useExplicitMatrix = false;
                (void)proxy.SetLocalTransform(proxyTransform);
                result.gizmo = gizmo.Draw(
                    proxy,
                    scene.GetCamera(),
                    state,
                    viewportRect);
                if (result.gizmo.changed &&
                    ApplyLocalTransform(
                        *activeObject,
                        proxy.GetTransform())) {
                    result.changedObjectIds.push_back(
                        activeObject->GetDocumentId());
                }
            } else {
                result.gizmo = gizmo.Draw(
                    *activeObject,
                    scene.GetCamera(),
                    state,
                    viewportRect);
                if (result.gizmo.changed) {
                    result.changedObjectIds.push_back(
                        activeObject->GetDocumentId());
                }
            }
            return result;
        }

        const MATH::Mat4 activeWorldBefore =
            activeObject->GetTransform().GetWorldMatrix();
        TransformData activeWorldTransform{};
        MATH::Quat activeWorldRotation{};
        if (!MATH::DecomposeTRS(
                activeWorldBefore,
                activeWorldTransform.position,
                activeWorldRotation,
                activeWorldTransform.scale)) {
            return result;
        }
        activeWorldTransform.rotationEulerDeg =
            MATH::EulerXYZDegreesFromQuat(activeWorldRotation);
        result.gizmo = gizmo.DrawTransform(
            activeWorldTransform,
            scene.GetCamera(),
            state,
            viewportRect);
        if (!result.gizmo.changed) {
            return result;
        }

        const MATH::Mat4 activeWorldAfter = MATH::Mat4::TRS(
            result.gizmo.transform.position,
            result.gizmo.rotation,
            result.gizmo.transform.scale);
        const MATH::Mat4 worldDelta =
            activeWorldAfter * MATH::Inverse(activeWorldBefore);

        std::unordered_set<uint64_t> selectedIds{};
        selectedIds.reserve(selectedObjects.size());
        for (const GameObject* object : selectedObjects) {
            selectedIds.insert(object->GetDocumentId().value);
        }

        for (GameObject* object : selectedObjects) {
            if (object == nullptr ||
                HasSelectedAncestor(*object, selectedIds)) {
                continue;
            }
            const MATH::Mat4 transformedWorld =
                worldDelta * object->GetTransform().GetWorldMatrix();
            Transform3D transformedLocal{};
            if (!DecomposeWorldToLocal(
                    *object,
                    transformedWorld,
                    transformedLocal)) {
                continue;
            }
            if (ApplyLocalTransform(
                    *object,
                    transformedLocal)) {
                result.changedObjectIds.push_back(
                    object->GetDocumentId());
            }
        }
        return result;
    }

    void CommitSceneSelectionTransforms(
        DocumentSceneBase& scene,
        const SceneSelectionTransformResult& result) {

        for (SceneObjectId objectId : result.changedObjectIds) {
            GameObject* object = scene.GetWorld().FindObject(objectId);
            if (object == nullptr) {
                continue;
            }
            if (IsCamera(*object)) {
                const Transform3D& transform = object->GetTransform();
                (void)scene.ApplyCameraObjectPose(
                    objectId,
                    transform.position,
                    transform.rotation,
                    true);
            }
            SyncDocumentTransform(scene, *object);
        }
    }

} // namespace HIKARI::EDITOR
