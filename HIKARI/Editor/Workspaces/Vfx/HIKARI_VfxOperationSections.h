#pragma once

namespace HIKARI::EDITOR {

    class EditorOperationRegistry;
    class VfxWorkspaceController;

    void RegisterVfxBindingOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller);

    void RegisterVfxCurveOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller);

    void RegisterVfxPreviewOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller);

} // namespace HIKARI::EDITOR
