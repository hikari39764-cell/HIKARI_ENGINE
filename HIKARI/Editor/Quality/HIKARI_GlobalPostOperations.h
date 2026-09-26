#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class GlobalPostEditor;

// 全体後処理のプロファイル資産を照会・複製・編集・保存する。編集はエディターの同じ文書に入り、
// シーンがどのプロファイルを使うかは environment.edit の post で選ぶ。
void RegisterGlobalPostOperations(EditorOperationRegistry& registry, GlobalPostEditor& editor,
                                  std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
