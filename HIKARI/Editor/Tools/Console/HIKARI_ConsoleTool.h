#pragma once

#include <memory>

#include "Editor/Tools/HIKARI_EditorTool.h"

namespace HIKARI::EDITOR {

// 開発コンソール（~ で開閉）。編集器の操作と実行時コマンドを同じ入力欄から実行する。
std::unique_ptr<IEditorTool> CreateConsoleTool();

} // namespace HIKARI::EDITOR
