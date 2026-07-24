#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/Prefab/HIKARI_PrefabRegistry.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"

namespace HIKARI {

    struct EditorContext;
    class DocumentSceneBase;
    class SelectionSyncService;

    namespace EDITOR {

        enum class SceneObjectCommandId {
            Rename,
            Duplicate,
            Delete,
            Lock,
            Unlock,
            SaveAsPrefab,
            InstantiatePrefab,
            AddComponent,
        };

        struct SceneObjectCommandPresentation {
            const char* label = "";
            const char* shortcut = "";
        };

        const SceneObjectCommandPresentation& GetSceneObjectCommandPresentation(
            SceneObjectCommandId command) noexcept;

        // Owns document mutations shared by hierarchy, inspector, viewport
        // menus and shortcuts. UI surfaces only choose a command and argument;
        // history, runtime synchronization and selection repair stay here.
        class SceneObjectCommandService {
        public:
            bool CanExecute(
                SceneObjectCommandId command,
                const DocumentSceneBase& scene,
                const EditorContext& context,
                std::string_view argument = {}) const;

            bool Execute(
                SceneObjectCommandId command,
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync,
                std::string_view argument = {});

            std::vector<std::string> ListPrefabIds() const;
            const std::string& GetStatusMessage() const noexcept;
            bool IsStatusError() const noexcept;
            void ClearStatus() noexcept;

            std::optional<SceneObjectAuthoringHistoryRequest>
                ConsumeHistoryRequest();

        private:
            void SetStatus(std::string message, bool error);

            DocumentComponentAuthoringService componentAuthoringService_{};
            PrefabRegistry prefabRegistry_{};
            PrefabSerializer prefabSerializer_{};
            std::optional<SceneObjectAuthoringHistoryRequest>
                historyRequest_{};
            std::string statusMessage_{};
            bool statusIsError_ = false;
        };

    } // namespace EDITOR
} // namespace HIKARI
