#include "HIKARI_SelectionSyncService.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "HIKARI_EditorContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#undef max
#undef min

namespace HIKARI {
    namespace {
        bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f) {
            return std::abs(lhs - rhs) <= epsilon;
        }

        bool NearlyEqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs, float epsilon = 0.001f) {
            return NearlyEqual(lhs.x, rhs.x, epsilon)
                && NearlyEqual(lhs.y, rhs.y, epsilon)
                && NearlyEqual(lhs.z, rhs.z, epsilon);
        }
    }

    SceneObjectData* SelectionSyncService::FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id) const {
        for (SceneObjectData& object : scene.GetSceneDocument().objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }

    SceneObjectData* SelectionSyncService::FindDocumentObjectByRuntime(DocumentSceneBase& scene, GameObject* runtimeObject) const {
        if (!runtimeObject) {
            return nullptr;
        }
        return FindDocumentObjectById(scene, runtimeObject->GetDocumentId());
    }

    GameObject* SelectionSyncService::FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id) const {
        return scene.GetWorld().FindObject(id);
    }

    bool SelectionSyncService::SyncSelectedObjectBackToDocument(DocumentSceneBase& scene, EditorSelection& selection, bool& sceneDirty, uint64_t& nextSceneObjectId) const {
        SceneObjectData* documentObject = FindDocumentObjectByRuntime(scene, selection.selectedObject);
        if (!documentObject || !selection.selectedObject) {
            return false;
        }

        bool changed = false;
        bool requiresRebuild = false;

        const Transform3D& runtimeTransform =
            selection.selectedObject->GetTransform();
        if (documentObject->transform.position.x != runtimeTransform.position.x
            || documentObject->transform.position.y != runtimeTransform.position.y
            || documentObject->transform.position.z != runtimeTransform.position.z) {
            documentObject->transform.position = runtimeTransform.position;
            changed = true;
        }
        if (documentObject->transform.scale.x != runtimeTransform.scale.x
            || documentObject->transform.scale.y != runtimeTransform.scale.y
            || documentObject->transform.scale.z != runtimeTransform.scale.z) {
            documentObject->transform.scale = runtimeTransform.scale;
            changed = true;
        }
        const MATH::Vec3 runtimeRotationEulerDeg =
            MATH::EulerXYZDegreesFromQuatNearest(
                runtimeTransform.rotation,
                documentObject->transform.rotationEulerDeg);
        if (!NearlyEqualVec3(documentObject->transform.rotationEulerDeg, runtimeRotationEulerDeg)) {
            // RuntimeのQuaternionを保存用Euler角へ同期する。
            documentObject->transform.rotationEulerDeg = runtimeRotationEulerDeg;
            changed = true;
        }

        const auto& runtimeComponents = selection.selectedObject->GetComponents();
        const size_t count = (std::min)(runtimeComponents.size(), documentObject->components.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& runtimeComponent = runtimeComponents[i];
            SceneComponentData& componentData = documentObject->components[i];
            if (!runtimeComponent || std::string(runtimeComponent->GetTypeName()) != componentData.type) {
                continue;
            }

            nlohmann::json serialized = nlohmann::json::object();
            runtimeComponent->Serialize(serialized);
            if (serialized == componentData.properties) {
                continue;
            }

            componentData.properties = std::move(serialized);
            changed = true;
            requiresRebuild = true;
        }

        if (changed) {
            sceneDirty = true;
        }
        if (requiresRebuild) {
            RebuildRuntimeWorldWithSelectionSync(scene, selection, nextSceneObjectId);
        }

        return changed;
    }

    bool SelectionSyncService::RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene, EditorSelection& selection, uint64_t& nextSceneObjectId) const {
        const SceneObjectId activeObjectId =
            selection.GetActiveObjectId();
        selection.selectedObject = nullptr;
        const bool built = scene.RebuildRuntimeWorld();
        selection.RepairObjectSelection(
            scene.GetWorld(),
            activeObjectId);
        selection.ClearAsset();
        SyncNextSceneObjectId(scene, nextSceneObjectId);
        return built;
    }

    void SelectionSyncService::SyncNextSceneObjectId(DocumentSceneBase& scene, uint64_t& nextSceneObjectId) const {
        uint64_t maxId = 0;
        for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
            maxId = std::max(maxId, object.id.value);
        }
        nextSceneObjectId = maxId + 1;
    }

} // namespace HIKARI
