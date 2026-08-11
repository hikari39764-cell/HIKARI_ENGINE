#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class CinematicsWorkspaceController;

// Registers Cinematics / Sequence Asset workspace operations on the unified
// editor operation bus. Human UI, AI actions and the local bridge all reach
// the workspace through these operations.
void RegisterCinematicsOperations(
    EditorOperationRegistry& registry,
    CinematicsWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
