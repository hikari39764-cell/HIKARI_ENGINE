#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {
constexpr SceneObjectCommandPresentation kPresentations[] = {
    {"editor.scene.rename", "F2"},
    {"editor.scene.duplicate", "Ctrl+D"},
    {"ui.common.delete", "Delete"},
    {"editor.scene.lock", ""},
    {"editor.scene.unlock", ""},
    {"editor.ui.save.as.prefab.5cb805", ""},
    {"editor.scene.instantiatePrefab", ""},
    {"editor.ui.add.component.08ba89", ""},
};

std::string SanitizePrefabToken(std::string_view raw) {
    std::string sanitized{};
    sanitized.reserve(raw.size());
    for (char ch : raw) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value) != 0 || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        } else if (!std::isspace(value)) {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? "NewPrefab" : sanitized;
}

SceneObjectData* FindSelectedDocumentObject(DocumentSceneBase& scene, const EditorContext& context,
                                            const SelectionSyncService& selectionSync) {
    if (context.selection.selectedObject == nullptr) {
        return nullptr;
    }
    return selectionSync.FindDocumentObjectByRuntime(scene, context.selection.selectedObject);
}

std::vector<SceneObjectId> CollectSelectedRootIds(const SceneDocument& document, const EditorSelection& selection) {
    std::unordered_set<uint64_t> selectedIds{};
    for (SceneObjectId objectId : selection.GetSelectedObjectIds()) {
        if (objectId.value != 0u) {
            selectedIds.insert(objectId.value);
        }
    }

    std::unordered_map<uint64_t, SceneObjectId> parents{};
    parents.reserve(document.objects.size());
    for (const SceneObjectData& object : document.objects) {
        if (object.parent) {
            parents.emplace(object.id.value, *object.parent);
        }
    }

    std::vector<SceneObjectId> roots{};
    roots.reserve(selectedIds.size());
    for (SceneObjectId objectId : selection.GetSelectedObjectIds()) {
        bool hasSelectedAncestor = false;
        SceneObjectId ancestor = objectId;
        std::unordered_set<uint64_t> visitedAncestors{objectId.value};
        while (true) {
            const auto found = parents.find(ancestor.value);
            if (found == parents.end()) {
                break;
            }
            ancestor = found->second;
            if (!visitedAncestors.insert(ancestor.value).second) {
                break;
            }
            if (selectedIds.contains(ancestor.value)) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor) {
            roots.push_back(objectId);
        }
    }
    return roots;
}

SceneObjectAuthoringHistoryRequest MakeHistory(std::string label, std::vector<SceneObjectData> beforeObjects,
                                               SceneCameraSettings beforeCamera, bool dirtyBefore,
                                               const DocumentSceneBase& scene, bool runtimeWorldAffected = true) {
    return SceneObjectAuthoringHistoryRequest{
        std::move(label),        std::move(beforeObjects),        scene.GetSceneDocument().objects,
        std::move(beforeCamera), scene.GetSceneDocument().camera, dirtyBefore,
        runtimeWorldAffected};
}
} // namespace

SceneObjectCommandPresentation GetSceneObjectCommandPresentation(SceneObjectCommandId command) noexcept {
    auto presentation = kPresentations[static_cast<size_t>(command)];
    presentation.label = TEXT::LocalizeCStr(presentation.label);
    return presentation;
}

bool SceneObjectCommandService::CanExecute(SceneObjectCommandId command, const DocumentSceneBase& scene,
                                           const EditorContext& context, std::string_view argument) const {
    const std::vector<SceneObjectId>& selectedIds = context.selection.GetSelectedObjectIds();
    switch (command) {
    case SceneObjectCommandId::InstantiatePrefab: return !argument.empty();
    case SceneObjectCommandId::Lock:
        return HasSelectedObjectWithEditorLock(scene.GetSceneDocument(), selectedIds, false);
    case SceneObjectCommandId::Unlock:
        return HasSelectedObjectWithEditorLock(scene.GetSceneDocument(), selectedIds, true);
    case SceneObjectCommandId::Duplicate:
    case SceneObjectCommandId::Delete:
        return context.selection.GetSelectedObjectCount() != 0u &&
               !HasSelectedObjectWithEditorLock(scene.GetSceneDocument(), selectedIds, true);
    case SceneObjectCommandId::Rename:
    case SceneObjectCommandId::SaveAsPrefab:
    case SceneObjectCommandId::AddComponent:
        return context.selection.selectedObject != nullptr &&
               !IsObjectEditorLocked(scene.GetSceneDocument(), context.selection.GetActiveObjectId()) &&
               (command != SceneObjectCommandId::AddComponent || !argument.empty());
    }
    return false;
}

bool SceneObjectCommandService::Execute(SceneObjectCommandId command, DocumentSceneBase& scene, EditorContext& context,
                                        const SelectionSyncService& selectionSync, std::string_view argument) {
    if (!CanExecute(command, scene, context, argument)) {
        SetStatus("Command is unavailable for the current selection.", true);
        return false;
    }

    SceneObjectData* target = FindSelectedDocumentObject(scene, context, selectionSync);
    if (command != SceneObjectCommandId::InstantiatePrefab && target == nullptr) {
        SetStatus("The selected object no longer exists.", true);
        return false;
    }

    const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
    std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;

    switch (command) {
    case SceneObjectCommandId::Rename: {
        const std::string newName(argument);
        if (newName.empty() || target->name == newName) {
            return false;
        }
        target->name = newName;
        context.selection.selectedObject->SetName(newName);
        context.sceneDirty = true;
        historyRequest_ = MakeHistory("Rename Object", std::move(beforeObjects), beforeCamera, dirtyBefore, scene);
        SetStatus("Renamed object to " + newName + ".", false);
        return true;
    }
    case SceneObjectCommandId::Duplicate: {
        const std::vector<SceneObjectId> sourceRootIds =
            CollectSelectedRootIds(scene.GetSceneDocument(), context.selection);
        if (sourceRootIds.empty()) {
            SetStatus("The selected objects no longer exist.", true);
            return false;
        }

        std::unordered_set<uint64_t> sourceRootIdValues{};
        std::unordered_set<uint64_t> subtreeIds{};
        for (SceneObjectId sourceRootId : sourceRootIds) {
            sourceRootIdValues.insert(sourceRootId.value);
            const std::unordered_set<uint64_t> subtree =
                CollectObjectSubtreeIds(scene.GetSceneDocument(), sourceRootId);
            subtreeIds.insert(subtree.begin(), subtree.end());
        }
        std::vector<SceneObjectData> sourceObjects;
        sourceObjects.reserve(subtreeIds.size());
        for (const SceneObjectData& source : beforeObjects) {
            if (!subtreeIds.contains(source.id.value)) {
                continue;
            }
            SceneObjectData duplicate = source;
            if (sourceRootIdValues.contains(source.id.value)) {
                duplicate.name += "_Copy";
            }
            sourceObjects.push_back(std::move(duplicate));
        }
        std::unordered_map<uint64_t, uint64_t> remappedIds;
        if (!AppendClonedSceneObjects(sourceObjects, scene.GetSceneDocument(), context.nextSceneObjectId,
                                      remappedIds)) {
            SetStatus("Could not clone the selected hierarchy identities.", true);
            return false;
        }

        std::vector<SceneObjectId> duplicateRootIds{};
        duplicateRootIds.reserve(sourceRootIds.size());
        for (SceneObjectId sourceRootId : sourceRootIds) {
            duplicateRootIds.push_back(SceneObjectId{remappedIds.at(sourceRootId.value)});
        }
        context.sceneDirty = true;
        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        context.selection.SelectObjectIds(scene.GetWorld(), duplicateRootIds);
        historyRequest_ = MakeHistory(sourceRootIds.size() == 1u ? "Duplicate Object" : "Duplicate Objects",
                                      std::move(beforeObjects), beforeCamera, dirtyBefore, scene);
        SetStatus(sourceRootIds.size() == 1u
                      ? "Duplicated selected object hierarchy."
                      : "Duplicated " + std::to_string(sourceRootIds.size()) + " selected object hierarchies.",
                  false);
        return true;
    }
    case SceneObjectCommandId::Delete: {
        const std::vector<SceneObjectId> sourceRootIds =
            CollectSelectedRootIds(scene.GetSceneDocument(), context.selection);
        std::unordered_set<uint64_t> deletedIds{};
        for (SceneObjectId sourceRootId : sourceRootIds) {
            const std::unordered_set<uint64_t> subtree =
                CollectObjectSubtreeIds(scene.GetSceneDocument(), sourceRootId);
            deletedIds.insert(subtree.begin(), subtree.end());
        }
        if (deletedIds.empty()) {
            SetStatus("The selected objects no longer exist.", true);
            return false;
        }
        const std::optional<SceneObjectId>& defaultCamera = scene.GetSceneDocument().camera.defaultCameraObjectId;
        if (defaultCamera && deletedIds.contains(defaultCamera->value)) {
            scene.ClearGameDefaultCamera();
        }
        if (scene.IsEditorCameraPreviewActive() && deletedIds.contains(scene.GetEditorCameraPreviewObjectId().value)) {
            scene.EndEditorCameraPreview();
        }
        auto& objects = scene.GetSceneDocument().objects;
        std::erase_if(objects,
                      [&deletedIds](const SceneObjectData& object) { return deletedIds.contains(object.id.value); });
        context.selection.ClearObjects();
        context.selection.ClearAsset();
        context.sceneDirty = true;
        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        historyRequest_ = MakeHistory(sourceRootIds.size() == 1u ? "Delete Object" : "Delete Objects",
                                      std::move(beforeObjects), beforeCamera, dirtyBefore, scene);
        SetStatus(sourceRootIds.size() == 1u
                      ? "Deleted selected object hierarchy."
                      : "Deleted " + std::to_string(sourceRootIds.size()) + " selected object hierarchies.",
                  false);
        return true;
    }
    case SceneObjectCommandId::Lock:
    case SceneObjectCommandId::Unlock: {
        const bool locked = command == SceneObjectCommandId::Lock;
        const std::size_t changedCount =
            SetObjectsEditorLocked(scene.GetSceneDocument(), context.selection.GetSelectedObjectIds(), locked);
        if (changedCount == 0u) {
            return false;
        }
        context.sceneDirty = true;
        historyRequest_ = MakeHistory(locked ? "Lock Objects" : "Unlock Objects", std::move(beforeObjects),
                                      beforeCamera, dirtyBefore, scene, false);
        SetStatus(std::string(locked ? "Locked " : "Unlocked ") + std::to_string(changedCount) +
                      (changedCount == 1u ? " object." : " objects."),
                  false);
        return true;
    }
    case SceneObjectCommandId::SaveAsPrefab: {
        const std::string prefabId = SanitizePrefabToken(argument.empty() ? target->name : argument);
        PrefabDocument prefab{};
        std::string prefabError;
        if (!CapturePrefabDocument(scene.GetSceneDocument(), target->id, prefabId, prefab, prefabError)) {
            SetStatus(prefabError, true);
            return false;
        }
        const bool saved = prefabRegistry_.Save(prefabId, prefab, prefabSerializer_);
        SetStatus(saved ? "Saved prefab: " + prefabId + "." : "Failed to save prefab: " + prefabId + ".", !saved);
        return saved;
    }
    case SceneObjectCommandId::InstantiatePrefab: {
        const auto selectedParent = context.selection.GetActiveObjectId();
        const auto previousNextId = context.nextSceneObjectId;
        PrefabDocument prefab{};
        const std::string prefabId(argument);
        if (!prefabRegistry_.Load(prefabId, prefab, prefabSerializer_)) {
            SetStatus("Failed to load prefab: " + prefabId + ".", true);
            return false;
        }
        SceneObjectId instanceId;
        std::string prefabError;
        if (!InstantiatePrefabDocument(prefab, prefabId, scene.GetSceneDocument(), context.nextSceneObjectId,
                                       instanceId, prefabError)) {
            SetStatus(prefabError, true);
            return false;
        }
        if (!AttachUiPrefabToCanvas(scene.GetSceneDocument(), scene.GetComponentRegistry(), instanceId, selectedParent,
                                    prefabError)) {
            scene.GetSceneDocument().objects = beforeObjects;
            context.nextSceneObjectId = previousNextId;
            SetStatus(prefabError, true);
            return false;
        }
        context.sceneDirty = true;
        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        context.selection.SelectObject(scene.GetWorld(),
                                       selectionSync.FindRuntimeObjectByDocumentId(scene, instanceId));
        historyRequest_ = MakeHistory("Instantiate Prefab", std::move(beforeObjects), beforeCamera, dirtyBefore, scene);
        SetStatus("Instantiated prefab: " + prefabId + ".", false);
        return true;
    }
    case SceneObjectCommandId::AddComponent: {
        const ComponentTypeInfo* typeInfo = scene.GetComponentRegistry().Find(argument);
        const ComponentAddResult result =
            componentAuthoringService_.AddComponent(scene.GetComponentRegistry(), *target, argument);
        SetStatus(result.message, !result.success);
        if (!result.success || !result.documentChanged) {
            return false;
        }
        context.sceneDirty = true;
        selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
        historyRequest_ =
            MakeHistory("Add " + (typeInfo != nullptr ? typeInfo->presentation.displayName : std::string(argument)),
                        std::move(beforeObjects), beforeCamera, dirtyBefore, scene);
        return true;
    }
    }
    return false;
}

std::vector<std::string> SceneObjectCommandService::ListPrefabIds() const {
    std::vector<std::string> ids = prefabRegistry_.ListPrefabIds();
    std::sort(ids.begin(), ids.end());
    return ids;
}

const std::string& SceneObjectCommandService::GetStatusMessage() const noexcept { return statusMessage_; }

bool SceneObjectCommandService::IsStatusError() const noexcept { return statusIsError_; }

void SceneObjectCommandService::ClearStatus() noexcept {
    statusMessage_.clear();
    statusIsError_ = false;
}

std::optional<SceneObjectAuthoringHistoryRequest> SceneObjectCommandService::ConsumeHistoryRequest() {
    std::optional<SceneObjectAuthoringHistoryRequest> request = std::move(historyRequest_);
    historyRequest_.reset();
    return request;
}

void SceneObjectCommandService::SetStatus(std::string message, bool error) {
    statusMessage_ = std::move(message);
    statusIsError_ = error;
}

} // namespace HIKARI::EDITOR
