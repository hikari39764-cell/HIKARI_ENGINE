#pragma once
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

#include <cstdint>
#include <filesystem>
#include <string>

#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

namespace HIKARI::EDITOR {

class RuntimeBindingEditorDocument final {
public:
    bool OpenProject(const std::filesystem::path& projectRoot, std::string& outMessage);
    bool Save(std::string& outMessage);

    bool ApplyCatalog(BINDING::Catalog catalog, std::string& outMessage, uint64_t mergeGroup = 0u);
    void SealMerge() noexcept;
    bool Undo();
    bool Redo();

    BINDING::Catalog& Catalog() noexcept;
    const BINDING::Catalog& Catalog() const noexcept;
    const std::filesystem::path& GetProjectRoot() const noexcept;
    const std::filesystem::path& GetCatalogPath() const noexcept;

    bool IsOpen() const noexcept;
    bool IsForProject(const std::filesystem::path& projectRoot) const noexcept;
    bool IsDirty() const noexcept;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;

private:
    EditorSourceVersion sourceVersion_;

    static bool AreEquivalent(const BINDING::Catalog& left, const BINDING::Catalog& right) noexcept;
    void RecordApplied(BINDING::Catalog before, uint64_t mergeGroup);
    void ResetHistory() noexcept;

    std::filesystem::path projectRoot_{};
    std::filesystem::path catalogPath_{};
    BINDING::Catalog catalog_{};
    DocumentSnapshotHistory<BINDING::Catalog> history_;
    bool open_ = false;
};

} // namespace HIKARI::EDITOR
