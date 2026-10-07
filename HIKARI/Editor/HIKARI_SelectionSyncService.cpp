#include "HIKARI_SelectionSyncService.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "HIKARI_EditorContext.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#undef max
#undef min

namespace HIKARI {
namespace {
bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f) { return std::abs(lhs - rhs) <= epsilon; }

} // namespace

SceneObjectData* SelectionSyncService::FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id) const {
    return FindSceneObjectData(scene.GetSceneDocument(), id);
}

SceneObjectData* SelectionSyncService::FindDocumentObjectByRuntime(DocumentSceneBase& scene,
                                                                   GameObject* runtimeObject) const {
    if (!runtimeObject) {
        return nullptr;
    }
    return FindDocumentObjectById(scene, runtimeObject->GetDocumentId());
}

GameObject* SelectionSyncService::FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id) const {
    return scene.GetWorld().FindObject(id);
}

bool SelectionSyncService::RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene, EditorSelection& selection,
                                                                uint64_t& nextSceneObjectId) const {
    const SceneObjectId activeObjectId = selection.GetActiveObjectId();
    const bool built = scene.RebuildRuntimeWorld();
    SynchronizeAfterRuntimeWorldRebuild(scene, selection, nextSceneObjectId, activeObjectId);
    return built;
}

bool SelectionSyncService::ApplyRuntimeObjectChangesWithSelectionSync(DocumentSceneBase& scene,
                                                                      EditorSelection& selection,
                                                                      uint64_t& nextSceneObjectId,
                                                                      const SceneObjectChanges& changes) const {
    const SceneObjectId activeObjectId = selection.GetActiveObjectId();
    const bool applied = scene.ApplyRuntimeObjectChanges(changes);
    SynchronizeAfterRuntimeWorldRebuild(scene, selection, nextSceneObjectId, activeObjectId);
    return applied;
}

void SelectionSyncService::SynchronizeAfterRuntimeWorldRebuild(DocumentSceneBase& scene, EditorSelection& selection,
                                                               uint64_t& nextSceneObjectId,
                                                               SceneObjectId activeObjectId) const {
    selection.selectedObject = nullptr;
    selection.RepairObjectSelection(scene.GetWorld(), activeObjectId);
    selection.ClearAsset();
    SyncNextSceneObjectId(scene, nextSceneObjectId);
}

void SelectionSyncService::SyncNextSceneObjectId(DocumentSceneBase& scene, uint64_t& nextSceneObjectId) const {
    uint64_t maxId = 0;
    for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
        maxId = std::max(maxId, object.id.value);
    }
    nextSceneObjectId = maxId + 1;
}

} // namespace HIKARI
