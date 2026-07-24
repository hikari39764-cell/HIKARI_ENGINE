#pragma once

#include <optional>

#include "Editor/Authoring/HIKARI_PrimitiveCreationDialog.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {

    struct EditorContext;
    class DocumentSceneBase;
    class SelectionSyncService;

    namespace EDITOR {
        class SceneObjectCommandService;

        class SceneCreationPanel {
        public:
            void DrawCreationMenu(
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync,
                SceneObjectCommandService& commands);

            void DrawDeferredDialogs(
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync);

            std::optional<SceneObjectAuthoringHistoryRequest>
                ConsumeHistoryRequest();

        private:
            void CreateEmptyObject(
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync);

            PrimitiveCreationDialog primitiveCreationDialog_{};
            std::optional<SceneObjectAuthoringHistoryRequest>
                historyRequest_{};
            char searchBuffer_[96]{};
        };

    } // namespace EDITOR
} // namespace HIKARI
