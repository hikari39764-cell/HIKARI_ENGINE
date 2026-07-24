#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    enum class EditorDocumentImpact : uint32_t {
        None = 0,
        Cinematics = 1u << 0u,
        RuntimeWorld = 1u << 1u,
        Environment = 1u << 2u,
        Systems = 1u << 3u,
    };

    struct EditorHistoryResult {
        EditorDocumentImpact impact = EditorDocumentImpact::None;
        std::string label{};
        bool changed = false;
    };

    class IEditorDocumentCommand {
    public:
        virtual ~IEditorDocumentCommand() = default;

        virtual const std::string& GetLabel() const noexcept = 0;
        virtual EditorDocumentImpact GetImpact() const noexcept = 0;
        virtual void Undo(SceneDocument& document) const = 0;
        virtual void Redo(SceneDocument& document) const = 0;
        virtual bool TryMergeApplied(
            const IEditorDocumentCommand& newer) = 0;
    };

    class EditorDocumentHistory {
    public:
        void Reset(uint64_t documentRevision, bool initiallyDirty = false);
        bool SyncDocumentRevision(
            uint64_t documentRevision,
            bool initiallyDirty = false);

        void RecordApplied(
            std::unique_ptr<IEditorDocumentCommand> command,
            uint64_t mergeGroup = 0);
        void SealMerge() noexcept;

        EditorHistoryResult Undo(SceneDocument& document);
        EditorHistoryResult Redo(SceneDocument& document);

        void MarkSaved() noexcept;
        bool IsDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        const std::string* GetUndoLabel() const noexcept;
        const std::string* GetRedoLabel() const noexcept;
        uint64_t GetDocumentRevision() const noexcept;

    private:
        struct Entry {
            std::unique_ptr<IEditorDocumentCommand> command{};
            uint64_t mergeGroup = 0;
        };

        std::vector<Entry> entries_{};
        size_t cursor_ = 0;
        std::optional<size_t> savedCursor_{ 0 };
        uint64_t documentRevision_ = 0;
        uint64_t openMergeGroup_ = 0;
        bool initialDirty_ = false;
        bool bound_ = false;
    };

} // namespace HIKARI::EDITOR
