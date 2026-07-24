#pragma once

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class DocumentSceneBase;
    class GameObject;
    struct EditorSelection;
    struct SceneObjectData;

    class SelectionSyncService {
    public:
        SceneObjectData* FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id) const;
        SceneObjectData* FindDocumentObjectByRuntime(DocumentSceneBase& scene, GameObject* runtimeObject) const;
        GameObject* FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id) const;

        bool SyncSelectedObjectBackToDocument(DocumentSceneBase& scene, EditorSelection& selection, bool& sceneDirty, uint64_t& nextSceneObjectId) const;
        bool RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene, EditorSelection& selection, uint64_t& nextSceneObjectId) const;
        void SyncNextSceneObjectId(DocumentSceneBase& scene, uint64_t& nextSceneObjectId) const;
    };

} // namespace HIKARI
