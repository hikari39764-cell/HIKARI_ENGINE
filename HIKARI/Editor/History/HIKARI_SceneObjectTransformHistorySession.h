#pragma once

#include <optional>
#include <string_view>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {

    struct SceneDocument;

    namespace EDITOR {

        // Converts one continuous viewport manipulation into one document
        // history entry. Runtime transforms can still update every frame.
        class SceneObjectTransformHistorySession {
        public:
            void BeginFrame() noexcept;

            void ObserveBeforeApply(
                const SceneDocument& document,
                SceneObjectId objectId,
                std::string_view label,
                bool manipulating,
                bool dirtyBefore);

            void MarkChanged() noexcept;
            void EndFrame(const SceneDocument& document);

            std::optional<SceneObjectAuthoringHistoryRequest>
                ConsumeHistoryRequest();

        private:
            struct PendingEdit {
                std::string label{};
                SceneObjectId objectId{};
                std::vector<SceneObjectData> beforeObjects{};
                SceneCameraSettings beforeCamera{};
                bool dirtyBefore = false;
                bool changed = false;
            };

            bool manipulatingThisFrame_ = false;
            std::optional<PendingEdit> pending_{};
            std::optional<SceneObjectAuthoringHistoryRequest> completed_{};
        };

    } // namespace EDITOR
} // namespace HIKARI
