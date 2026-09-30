#pragma once

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "HIKARI/Project/EditorOperations.h"

namespace HIKARI::EDITOR {

class GameProjectEditorOperationRegistration final : public EDITOR_PROJECT::IEditorOperationRegistrar {
public:
    GameProjectEditorOperationRegistration(EditorOperationRegistry& registry, EditorOperationOwnerToken ownerToken,
                                           EDITOR_PROJECT::IEditorDocumentService* documents);

    EDITOR_PROJECT::EditorOperationRegistrationResult RegisterOperation(
        const EDITOR_PROJECT::EditorOperationDescriptor& descriptor) override;
    EDITOR_PROJECT::IEditorDocumentService* Documents() noexcept override;

    bool Failed() const noexcept;
    const std::string& FailureMessage() const noexcept;

private:
    EDITOR_PROJECT::EditorOperationRegistrationResult Fail(EDITOR_PROJECT::EditorOperationRegistrationCode code,
                                                           std::string message);

    EditorOperationRegistry& registry_;
    EditorOperationOwnerToken ownerToken_ = 0u;
    EDITOR_PROJECT::IEditorDocumentService* documents_ = nullptr;
    bool failed_ = false;
    std::string failureMessage_{};
};

} // namespace HIKARI::EDITOR
