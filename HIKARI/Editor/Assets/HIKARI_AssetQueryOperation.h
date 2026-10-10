#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
class EditorOperationRegistry;

// 資産カタログの検索 asset.query（種類と名前・パスの一部で探し、GUID・種類・名前・ソースパスを返す）を登録する。
void RegisterAssetQueryOperation(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
