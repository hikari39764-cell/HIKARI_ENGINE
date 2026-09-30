#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "HIKARI/Project/EditorDocuments.h"

namespace HIKARI::EDITOR {

class GameProjectEditorDocumentService final : public EDITOR_PROJECT::IEditorDocumentService {
public:
    explicit GameProjectEditorDocumentService(std::filesystem::path dataRoot);
    ~GameProjectEditorDocumentService() override;

    GameProjectEditorDocumentService(const GameProjectEditorDocumentService&) = delete;
    GameProjectEditorDocumentService& operator=(const GameProjectEditorDocumentService&) = delete;

    EDITOR_PROJECT::EditorDocumentResult Open(std::string_view relativePath) override;
    EDITOR_PROJECT::EditorDocumentResult Create(std::string_view relativePath,
                                                std::string_view initialUtf8Text) override;
    EDITOR_PROJECT::EditorDocumentResult Read(EDITOR_PROJECT::EditorDocumentHandle document, uint64_t expectedRevision,
                                              EDITOR_PROJECT::IEditorDocumentReadSink& sink) const override;
    EDITOR_PROJECT::EditorDocumentResult Apply(EDITOR_PROJECT::EditorDocumentHandle document, uint64_t expectedRevision,
                                               std::string_view validatedUtf8Text) override;
    EDITOR_PROJECT::EditorDocumentResult Undo(EDITOR_PROJECT::EditorDocumentHandle document,
                                              uint64_t expectedRevision) override;
    EDITOR_PROJECT::EditorDocumentResult Redo(EDITOR_PROJECT::EditorDocumentHandle document,
                                              uint64_t expectedRevision) override;
    EDITOR_PROJECT::EditorDocumentResult Save(EDITOR_PROJECT::EditorDocumentHandle document,
                                              uint64_t expectedRevision) override;
    EDITOR_PROJECT::EditorDocumentResult Close(EDITOR_PROJECT::EditorDocumentHandle document,
                                               bool discardChanges) override;

    bool HasUnsavedDocuments() const noexcept;
    void CloseAll() noexcept;

private:
    struct Document;

    Document* Find(EDITOR_PROJECT::EditorDocumentHandle handle) noexcept;
    const Document* Find(EDITOR_PROJECT::EditorDocumentHandle handle) const noexcept;
    Document* FindPath(const std::filesystem::path& path) noexcept;
    EDITOR_PROJECT::EditorDocumentResult MakeResult(EDITOR_PROJECT::EditorDocumentCode code,
                                                    const Document* document) const noexcept;

    std::filesystem::path dataRoot_{};
    std::vector<std::unique_ptr<Document>> documents_{};
};

} // namespace HIKARI::EDITOR
