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
    Lighting = 1u << 4u,
};

constexpr EditorDocumentImpact operator|(EditorDocumentImpact lhs, EditorDocumentImpact rhs) noexcept {
    return static_cast<EditorDocumentImpact>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr bool HasEditorDocumentImpact(EditorDocumentImpact value, EditorDocumentImpact flag) noexcept {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0u;
}

struct EditorHistoryResult {
    EditorDocumentImpact impact = EditorDocumentImpact::None;
    std::string label{};
    bool changed = false;
    // オブジェクト単位の差分が分かるコマンドだけが持つ。無ければ runtime world を作り直す。
    std::optional<SceneObjectChanges> objectChanges{};
};

class IEditorDocumentCommand {
public:
    virtual ~IEditorDocumentCommand() = default;

    virtual const std::string& GetLabel() const noexcept = 0;
    virtual EditorDocumentImpact GetImpact() const noexcept = 0;
    virtual void Undo(SceneDocument& document) const = 0;
    virtual void Redo(SceneDocument& document) const = 0;
    virtual bool TryMergeApplied(const IEditorDocumentCommand& newer) = 0;
    // 撤销（undo=true）または重做で変わるオブジェクト。分からないコマンドは nullopt。
    virtual std::optional<SceneObjectChanges> GetObjectChanges(bool) const { return std::nullopt; }
};

class EditorDocumentHistory {
public:
    void Reset(uint64_t documentRevision, bool initiallyDirty = false);
    bool SyncDocumentRevision(uint64_t documentRevision, bool initiallyDirty = false);

    void RecordApplied(std::unique_ptr<IEditorDocumentCommand> command, uint64_t mergeGroup = 0);
    void SealMerge() noexcept;
    // 開いている間に記録したコマンドは種類が違っても一つの単位で撤销・重做する（AI の一括操作や一回の依頼）。
    // 入れ子にできる。外側の EndUndoGroup で閉じる。reuse に前に返した組を渡すと、同じ組へ続けて記録する
    // （智能体の一回の依頼の中の、別々の呼び出し）。開いた組を返す。
    uint64_t BeginUndoGroup(uint64_t reuse = 0) noexcept;
    void EndUndoGroup() noexcept;
    // 次に撤销される記録の組（組に属さなければ 0）。
    uint64_t GetUndoGroup() const noexcept;

    EditorHistoryResult Undo(SceneDocument& document);
    EditorHistoryResult Redo(SceneDocument& document);
    // 撤销できる記録の数。UndoTo に渡して、その時点以降の記録だけを組に関係なく戻す（失敗した一括操作の巻き戻し）。
    size_t GetUndoDepth() const noexcept { return cursor_; }
    EditorHistoryResult UndoTo(SceneDocument& document, size_t depth);

    void MarkSaved() noexcept;
    bool IsDirty() const noexcept;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    const std::string* GetUndoLabel() const noexcept;
    const std::string* GetRedoLabel() const noexcept;
    uint64_t GetEditRevision() const noexcept { return editRevision_; }

private:
    struct Entry {
        std::unique_ptr<IEditorDocumentCommand> command{};
        uint64_t mergeGroup = 0;
        uint64_t undoGroup = 0;
    };

    std::vector<Entry> entries_{};
    size_t cursor_ = 0;
    std::optional<size_t> savedCursor_{0};
    uint64_t documentRevision_ = 0;
    uint64_t editRevision_ = 1;
    uint64_t openMergeGroup_ = 0;
    uint64_t openUndoGroup_ = 0;
    uint64_t nextUndoGroup_ = 1;
    uint32_t undoGroupDepth_ = 0;
    bool initialDirty_ = false;
    bool bound_ = false;
};

} // namespace HIKARI::EDITOR
