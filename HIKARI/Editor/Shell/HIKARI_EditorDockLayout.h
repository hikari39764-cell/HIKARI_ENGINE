#pragma once
#include "imgui.h"

namespace HIKARI::EDITOR {
// ウィンドウの縮小やサイドバー開閉でも、既存の分割比率を維持する。
ImGuiID DrawWorkspaceDockSpace(ImGuiID id, const ImGuiViewport* viewport,
                               ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_None);
} // namespace HIKARI::EDITOR
