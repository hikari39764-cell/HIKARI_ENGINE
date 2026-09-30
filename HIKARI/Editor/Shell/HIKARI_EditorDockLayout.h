#pragma once
#include "imgui.h"

namespace HIKARI::EDITOR {
// ウィンドウの縮小やサイドバー開閉でも、既存の分割比率を維持する。
ImGuiID DrawWorkspaceDockSpace(ImGuiID id, const ImGuiViewport* viewport,
                               ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_None);
// ワークスペース描画後、対象ウィンドウの Begin 前に呼ぶ。非表示のドックから明示的に再表示する。
void RevealEditorWindow(const char* windowName);
} // namespace HIKARI::EDITOR
