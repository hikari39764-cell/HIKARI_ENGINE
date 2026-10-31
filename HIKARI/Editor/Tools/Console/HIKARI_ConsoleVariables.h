#pragma once

namespace HIKARI::EDITOR {

class EditorOperationRegistry;

// コンソール変数の操作 set / get / reset / cvars を登録する。変数は設定構造体の項目表から作り、名前は接頭辞 + 項目のパス
// （r. = 画質、gfx. = Gfx 診断。例 r.directionalShadow.resolution）。今の設定を変えるだけでファイルには書かない。
// r. は画質パネルと同じ今の画質なので、パネルの保存や Play ボタンの時に一緒に保存される。
void RegisterConsoleVariableOperations(EditorOperationRegistry& registry);

} // namespace HIKARI::EDITOR
