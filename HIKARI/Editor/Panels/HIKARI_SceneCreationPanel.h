#pragma once

#include <cstdint>
#include <optional>

#include "Editor/Authoring/HIKARI_PrimitiveCreationDialog.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {

    namespace RENDER3D::LIGHTING {
        enum class SceneLightType : uint8_t;
    }

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
            void CreateLightingObject(
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync,
                bool reflectionProbe);
            void CreateLightObject(
                DocumentSceneBase& scene,
                EditorContext& context,
                const SelectionSyncService& selectionSync,
                RENDER3D::LIGHTING::SceneLightType lightType);

            PrimitiveCreationDialog primitiveCreationDialog_{};
            std::optional<SceneObjectAuthoringHistoryRequest>
                historyRequest_{};
            char searchBuffer_[96]{};
        };

    } // namespace EDITOR
} // namespace HIKARI
