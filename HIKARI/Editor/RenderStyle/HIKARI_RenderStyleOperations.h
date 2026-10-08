#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class RenderStyleEditor;

// 描画の様式の資産を照会・複製・編集・保存する。編集はエディターの同じ文書に入り、シーンがどの様式を使うかは
// environment.edit の renderStyle で選ぶ。層が選ぶ後処理プロファイルの値は post.profile.* で編集する。
void RegisterRenderStyleOperations(EditorOperationRegistry& registry, RenderStyleEditor& editor,
                                   std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
