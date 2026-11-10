#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "HIKARI/Project/EditorDocuments.h"

namespace HIKARI {
class AssetDatabase;
}

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
    EDITOR_PROJECT::EditorDocumentResult OpenAsset(std::string_view assetGuid) override;
    EDITOR_PROJECT::EditorDocumentResult CreateAsset(std::string_view assetsRelativePath,
                                                     std::string_view initialUtf8Text) override;
    size_t GetAssetGuid(EDITOR_PROJECT::EditorDocumentHandle document, char* utf8Buffer,
                        size_t bufferCapacity) const override;

    // 資産の文書（OpenAsset / CreateAsset）が読み書きする資産庫。宿主がフレームごとに渡す。
    void SetAssetDatabase(AssetDatabase* database);
    bool HasUnsavedDocuments() const noexcept;
    void CloseAll() noexcept;

private:
    struct Document;

    Document* Find(EDITOR_PROJECT::EditorDocumentHandle handle) noexcept;
    const Document* Find(EDITOR_PROJECT::EditorDocumentHandle handle) const noexcept;
    Document* FindPath(const std::filesystem::path& path) noexcept;
    // 文書の路径の基準：Data の文書は Data フォルダ、資産の文書は Assets フォルダ。
    const std::filesystem::path& RootOf(bool asset) const noexcept { return asset ? assetsRoot_ : dataRoot_; }
    EDITOR_PROJECT::EditorDocumentResult OpenAt(std::string_view relativePath, bool asset);
    EDITOR_PROJECT::EditorDocumentResult CreateAt(std::string_view relativePath, std::string_view initialUtf8Text,
                                                  bool asset);
    // 保存した資産を資産庫に読み直させる。新しいファイルはここで GUID が付く。
    void PublishSavedAsset(Document& document);
    EDITOR_PROJECT::EditorDocumentResult MakeResult(EDITOR_PROJECT::EditorDocumentCode code,
                                                    const Document* document) const noexcept;

    std::filesystem::path dataRoot_{};
    std::filesystem::path assetsRoot_{};
    AssetDatabase* assetDatabase_ = nullptr;
    std::vector<std::unique_ptr<Document>> documents_{};
};

} // namespace HIKARI::EDITOR
