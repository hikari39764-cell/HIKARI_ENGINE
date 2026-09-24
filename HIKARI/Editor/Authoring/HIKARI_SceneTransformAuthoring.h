#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {
nlohmann::json DescribeSceneObjectTransform(const SceneObjectData& object);
// Stages all edits before publishing. Optional expected values implement
// optimistic concurrency for tools retaining a local draft.
EditorOperationResult StageSceneTransformEdits(const SceneDocument& document, const nlohmann::json& commands,
                                               std::vector<SceneObjectData>& output);
} // namespace HIKARI::EDITOR
