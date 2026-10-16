#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class DocumentSceneBase;
class GameObject;
struct EditorSelection;

namespace EDITOR {

struct SceneSelectionTransformResult {
    EditorTransformGizmoResult gizmo{};
    std::vector<SceneObjectId> changedObjectIds{};
    bool lightingRuntimeRefreshRequired = false;
    std::string_view historyLabelOverride{};
};

// Draws one active-object gizmo and applies its world-space delta to
// each selected hierarchy root. Selected descendants are not applied
// twice when their parent is also selected.
// 祖先のどれかが selectedIds（文書 ID）に含まれるか。選択した階層の根だけを処理する時に使う。
bool HasSelectedAncestor(const GameObject& object, const std::unordered_set<uint64_t>& selectedIds);

SceneSelectionTransformResult DrawSceneSelectionTransformGizmo(DocumentSceneBase& scene, EditorSelection& selection,
                                                               const EditorTransformGizmo& gizmo,
                                                               const EditorTransformGizmoState& state,
                                                               const EditorViewportRect& viewportRect);

void CommitSceneSelectionTransforms(DocumentSceneBase& scene, const SceneSelectionTransformResult& result);

} // namespace EDITOR
} // namespace HIKARI
