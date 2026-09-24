#pragma once
#include <functional>
namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class VfxWorkspaceController;

void RegisterVfxBindingOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller);

void RegisterVfxCurveOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller);

void RegisterVfxPreviewOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller);

void RegisterVfxLibraryOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                                  std::function<AssetDatabase*()> databaseProvider);

} // namespace HIKARI::EDITOR
