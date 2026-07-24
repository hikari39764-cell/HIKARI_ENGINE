#pragma once

#include <vector>

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct EditorSelection;

    namespace EDITOR {

        struct SceneSelectionTransformResult {
            EditorTransformGizmoResult gizmo{};
            std::vector<SceneObjectId> changedObjectIds{};
        };

        // Draws one active-object gizmo and applies its world-space delta to
        // each selected hierarchy root. Selected descendants are not applied
        // twice when their parent is also selected.
        SceneSelectionTransformResult DrawSceneSelectionTransformGizmo(
            DocumentSceneBase& scene,
            EditorSelection& selection,
            const EditorTransformGizmo& gizmo,
            const EditorTransformGizmoState& state,
            const EditorViewportRect& viewportRect);

        void CommitSceneSelectionTransforms(
            DocumentSceneBase& scene,
            const SceneSelectionTransformResult& result);

    } // namespace EDITOR
} // namespace HIKARI
