#pragma once
#include <string>

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

// 資産文書の共通の窓口。型を問わず、全部保存・未保存の一覧・焦点のある窓への元に戻すの振り分けに使う。
class EditorAssetDocument {
public:
    virtual bool IsOpen() const = 0;
    virtual bool IsDirty() const = 0;
    virtual bool CanUndo() const = 0;
    virtual bool CanRedo() const = 0;
    virtual bool Undo() = 0;
    virtual bool Redo() = 0;
    // 開いている資産へ上書き保存する（新規の下書きは資産として作る）。
    virtual bool SaveDocument(AssetDatabase& database, std::string& message) = 0;
    virtual std::string DisplayName() const = 0;

protected:
    ~EditorAssetDocument() = default;
};

} // namespace HIKARI::EDITOR
