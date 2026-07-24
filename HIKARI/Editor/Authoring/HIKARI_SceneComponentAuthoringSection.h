#pragma once

#include <optional>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Editor/HIKARI_ComponentDocumentEditor.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"

namespace HIKARI {

    struct EditorContext;
    class DocumentSceneBase;
    class SelectionSyncService;

    class SceneComponentAuthoringSection {
    public:
        std::optional<SceneObjectAuthoringHistoryRequest> Draw(
            DocumentSceneBase& scene,
            EditorContext& context,
            const SelectionSyncService& selectionSync,
            SceneObjectData& target);

    private:
        struct DeferredRuntimeComponentApply {
            SceneObjectId objectId{};
            size_t componentIndex = 0;
        };

        struct PendingComponentHistory {
            std::string label{};
            std::vector<SceneObjectData> beforeObjects{};
            SceneCameraSettings beforeCamera{};
            bool dirtyBefore = false;
        };

        ComponentDocumentEditor componentDocumentEditor_{};
        DocumentComponentAuthoringService componentAuthoringService_{};
        ImGuiInspectorBuilder componentInspectorBuilder_{};
        std::optional<DeferredRuntimeComponentApply>
            deferredRuntimeComponentApply_{};
        std::optional<PendingComponentHistory>
            pendingComponentHistory_{};
        bool deferredComponentRebuild_ = false;
    };

} // namespace HIKARI
