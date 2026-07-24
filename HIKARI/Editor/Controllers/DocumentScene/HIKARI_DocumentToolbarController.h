#pragma once

namespace HIKARI {

    struct EditorContext;
    class DocumentSceneBase;
    class SelectionSyncService;

    class DocumentToolbarController {
    public:
        void SyncDocumentMeta(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const;
        void Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const;
        void DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const;
    };

} // namespace HIKARI
