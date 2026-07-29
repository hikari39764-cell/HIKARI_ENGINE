#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
    namespace {
        enum class LightingComponentScaleTarget {
            None,
            ReflectionInfluenceBox,
            ReflectionInfluenceSphere,
            ReflectionProjectionBox,
            IrradianceVolume
        };

        struct LightingComponentScaleProxy {
            LightingComponentScaleTarget target =
                LightingComponentScaleTarget::None;
            TransformData transform{};
            std::string_view historyLabel{};
        };

        bool IsCamera(const GameObject& object) {
            return object.GetComponent<CameraComponent>() != nullptr;
        }

        LightingComponentScaleProxy BuildLightingComponentScaleProxy(
            const GameObject& object) {

            LightingComponentScaleProxy proxy{};
            const ReflectionProbeComponent* reflectionProbe =
                object.GetComponent<ReflectionProbeComponent>();
            const IrradianceVolumeComponent* irradianceVolume =
                object.GetComponent<IrradianceVolumeComponent>();
            if (reflectionProbe != nullptr &&
                irradianceVolume != nullptr) {
                return proxy;
            }

            proxy.transform.position =
                object.GetTransform().position;
            if (reflectionProbe != nullptr) {
                if (!reflectionProbe->
                        GetProjectionFollowsInfluence() &&
                    reflectionProbe->GetViewportBoundsTarget() ==
                        ReflectionProbeViewportBoundsTarget::Projection &&
                    reflectionProbe->GetProjectionShape() ==
                        ReflectionProbeProjectionShape::Box) {

                    proxy.target = LightingComponentScaleTarget::
                        ReflectionProjectionBox;
                    proxy.transform.position =
                        proxy.transform.position +
                        reflectionProbe->
                            GetProjectionBoxOffset();
                    proxy.transform.scale =
                        reflectionProbe->GetProjectionBoxSize();
                    proxy.historyLabel =
                        "Resize Reflection Projection Bounds";
                    return proxy;
                }

                proxy.historyLabel =
                    reflectionProbe->GetProjectionFollowsInfluence()
                        ? "Resize Linked Reflection Bounds"
                        : "Resize Reflection Influence";
                if (reflectionProbe->GetInfluenceShape() ==
                    ReflectionProbeInfluenceShape::Box) {
                    proxy.target = LightingComponentScaleTarget::
                        ReflectionInfluenceBox;
                    proxy.transform.position =
                        proxy.transform.position +
                        reflectionProbe->GetInfluenceBoxOffset();
                    proxy.transform.scale =
                        reflectionProbe->GetInfluenceBoxSize();
                } else {
                    proxy.target = LightingComponentScaleTarget::
                        ReflectionInfluenceSphere;
                    const float diameter =
                        reflectionProbe->GetRadius() * 2.0f;
                    proxy.transform.scale = {
                        diameter,
                        diameter,
                        diameter
                    };
                }
                return proxy;
            }

            if (irradianceVolume != nullptr) {
                proxy.target =
                    LightingComponentScaleTarget::IrradianceVolume;
                proxy.transform.scale =
                    irradianceVolume->GetSize();
                proxy.historyLabel = "Resize Irradiance Volume";
            }
            return proxy;
        }

        MATH::Vec3 PositiveSize(
            const MATH::Vec3& value) noexcept {

            return {
                std::abs(value.x),
                std::abs(value.y),
                std::abs(value.z)
            };
        }

        float ResolveEditedSphereRadius(
            float originalRadius,
            const MATH::Vec3& editedScale) noexcept {

            const float originalDiameter = originalRadius * 2.0f;
            const MATH::Vec3 diameter = PositiveSize(editedScale);
            float selectedDiameter = diameter.x;
            float selectedDelta =
                std::abs(diameter.x - originalDiameter);
            const auto selectIfFarther =
                [&](float candidate) {
                    const float delta =
                        std::abs(candidate - originalDiameter);
                    if (delta > selectedDelta) {
                        selectedDiameter = candidate;
                        selectedDelta = delta;
                    }
                };
            selectIfFarther(diameter.y);
            selectIfFarther(diameter.z);
            return selectedDiameter * 0.5f;
        }

        bool ApplyLightingComponentScale(
            GameObject& object,
            LightingComponentScaleTarget target,
            const MATH::Vec3& editedScale) {

            switch (target) {
            case LightingComponentScaleTarget::
                    ReflectionInfluenceBox: {
                ReflectionProbeComponent* probe =
                    object.GetComponent<ReflectionProbeComponent>();
                return probe != nullptr &&
                    probe->SetInfluenceBoxSize(
                        PositiveSize(editedScale));
            }
            case LightingComponentScaleTarget::
                    ReflectionInfluenceSphere: {
                ReflectionProbeComponent* probe =
                    object.GetComponent<ReflectionProbeComponent>();
                return probe != nullptr &&
                    probe->SetRadius(
                        ResolveEditedSphereRadius(
                            probe->GetRadius(),
                            editedScale));
            }
            case LightingComponentScaleTarget::
                    ReflectionProjectionBox: {
                ReflectionProbeComponent* probe =
                    object.GetComponent<ReflectionProbeComponent>();
                return probe != nullptr &&
                    probe->SetProjectionBoxSize(
                        PositiveSize(editedScale));
            }
            case LightingComponentScaleTarget::IrradianceVolume: {
                IrradianceVolumeComponent* volume =
                    object.GetComponent<IrradianceVolumeComponent>();
                return volume != nullptr &&
                    volume->SetSize(PositiveSize(editedScale));
            }
            case LightingComponentScaleTarget::None:
            default:
                return false;
            }
        }

        bool DrawLightingComponentScaleGizmo(
            DocumentSceneBase& scene,
            GameObject& object,
            const EditorTransformGizmo& gizmo,
            const EditorTransformGizmoState& state,
            const EditorViewportRect& viewportRect,
            SceneSelectionTransformResult& result) {

            if (state.operation !=
                EditorTransformGizmoOperation::Scale) {
                return false;
            }
            const LightingComponentScaleProxy proxy =
                BuildLightingComponentScaleProxy(object);
            if (proxy.target ==
                LightingComponentScaleTarget::None) {
                return false;
            }

            result.historyLabelOverride = proxy.historyLabel;
            result.gizmo = gizmo.DrawTransform(
                proxy.transform,
                scene.GetCamera(),
                state,
                viewportRect);
            if (result.gizmo.changed &&
                ApplyLightingComponentScale(
                    object,
                    proxy.target,
                    result.gizmo.transform.scale)) {
                result.changedObjectIds.push_back(
                    object.GetDocumentId());
                result.lightingRuntimeRefreshRequired = true;
            }
            return true;
        }

        template<class Component>
        void SyncDocumentComponent(
            SceneObjectData& documentObject,
            const Component& runtimeComponent) {

            for (SceneComponentData& documentComponent :
                documentObject.components) {
                if (documentComponent.type !=
                    runtimeComponent.GetTypeName()) {
                    continue;
                }
                nlohmann::json properties =
                    nlohmann::json::object();
                runtimeComponent.Serialize(properties);
                documentComponent.properties =
                    std::move(properties);
                return;
            }
        }

        void SyncLightingComponentProperties(
            DocumentSceneBase& scene,
            const GameObject& object) {

            for (SceneObjectData& documentObject :
                scene.GetSceneDocument().objects) {
                if (documentObject.id != object.GetDocumentId()) {
                    continue;
                }
                if (const auto* probe =
                        object.GetComponent<
                            ReflectionProbeComponent>()) {
                    SyncDocumentComponent(
                        documentObject,
                        *probe);
                }
                if (const auto* volume =
                        object.GetComponent<
                            IrradianceVolumeComponent>()) {
                    SyncDocumentComponent(
                        documentObject,
                        *volume);
                }
                return;
            }
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
            if (DrawLightingComponentScaleGizmo(
                    scene,
                    *activeObject,
                    gizmo,
                    state,
                    viewportRect,
                    result)) {
                return result;
            }
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
            if (result.lightingRuntimeRefreshRequired) {
                SyncLightingComponentProperties(
                    scene,
                    *object);
            }
        }
    }

} // namespace HIKARI::EDITOR
