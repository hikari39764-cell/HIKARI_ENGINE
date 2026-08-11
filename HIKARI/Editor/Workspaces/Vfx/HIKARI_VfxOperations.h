#pragma once

#include <functional>

namespace HIKARI {
    class AssetDatabase;
}

namespace HIKARI::EDITOR {

    class EditorOperationRegistry;
    class VfxWorkspaceController;

    void RegisterVfxOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller,
        std::function<AssetDatabase*()> assetDatabaseProvider);

} // namespace HIKARI::EDITOR
