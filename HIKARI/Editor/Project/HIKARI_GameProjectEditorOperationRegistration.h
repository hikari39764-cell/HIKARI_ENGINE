#pragma once

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "HIKARI/Project/EditorOperations.h"

namespace HIKARI::EDITOR {

class GameProjectEditorWorkspaceRegistry;

class GameProjectEditorOperationRegistration final : public EDITOR_PROJECT::IEditorOperationRegistrar {
public:
    // commands は処理中に他の Editor 操作を呼ぶ入口（項目の工作区の登録表）。無ければ処理から操作を呼べない。
    GameProjectEditorOperationRegistration(EditorOperationRegistry& registry, EditorOperationOwnerToken ownerToken,
                                           EDITOR_PROJECT::IEditorDocumentService* documents,
                                           GameProjectEditorWorkspaceRegistry* commands = nullptr,
                                           EDITOR_PROJECT::IEditorSceneServices* sceneServices = nullptr);

    EDITOR_PROJECT::EditorOperationRegistrationResult RegisterOperation(
        const EDITOR_PROJECT::EditorOperationDescriptor& descriptor) override;
    EDITOR_PROJECT::IEditorDocumentService* Documents() noexcept override;
    EDITOR_PROJECT::IEditorWorkspaceCommands* Commands() noexcept override;
    EDITOR_PROJECT::IEditorSceneServices* SceneServices() noexcept override { return sceneServices_; }

    bool Failed() const noexcept;
    const std::string& FailureMessage() const noexcept;

private:
    EDITOR_PROJECT::EditorOperationRegistrationResult Fail(EDITOR_PROJECT::EditorOperationRegistrationCode code,
                                                           std::string message);

    EditorOperationRegistry& registry_;
    EditorOperationOwnerToken ownerToken_ = 0u;
    EDITOR_PROJECT::IEditorDocumentService* documents_ = nullptr;
    GameProjectEditorWorkspaceRegistry* commands_ = nullptr;
    EDITOR_PROJECT::IEditorSceneServices* sceneServices_ = nullptr;
    bool failed_ = false;
    std::string failureMessage_{};
};

} // namespace HIKARI::EDITOR
