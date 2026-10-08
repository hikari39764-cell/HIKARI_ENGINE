#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class EnvironmentAssetEditor;

// 環境プロファイルと天気の資産を照会・複製・編集・保存する。編集はエディターの同じ文書に入り、
// 保存前から下書きとしてシーンへ映る。シーンがどの資産を使うかは environment.edit で選ぶ。
void RegisterEnvironmentAssetOperations(EditorOperationRegistry& registry, EnvironmentAssetEditor& editor,
                                        std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
