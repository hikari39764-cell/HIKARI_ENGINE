#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include <cmath>
#include "imgui_internal.h"

namespace HIKARI::EDITOR {
namespace {
void ResizeReferences(ImGuiDockNode* node, ImVec2 ratio) {
    // 中央ノードだけが縮む既定ポリシーを避ける。ドッキング先、タブ、分割方向は変更しない。
    // 設定読込直後の子ノードは Size が未計算。保存済み SizeRef を基準にする。
    const auto size = node->Size.x > 1 && node->Size.y > 1 ? node->Size : node->SizeRef;
    node->SizeRef = {size.x * ratio.x, size.y * ratio.y};
    for (auto* child : node->ChildNodes)
        if (child)
            ResizeReferences(child, ratio);
}
} // namespace

ImGuiID DrawWorkspaceDockSpace(ImGuiID id, const ImGuiViewport* viewport, ImGuiDockNodeFlags flags) {
    if (auto* root = ImGui::DockBuilderGetNode(id); root && root->Size.x > 1 && root->Size.y > 1 &&
                                                    viewport->WorkSize.x > 1 && viewport->WorkSize.y > 1 &&
                                                    (std::abs(root->Size.x - viewport->WorkSize.x) > 0.5f ||
                                                     std::abs(root->Size.y - viewport->WorkSize.y) > 0.5f)) {
        ResizeReferences(root, {viewport->WorkSize.x / root->Size.x, viewport->WorkSize.y / root->Size.y});
    }
    return ImGui::DockSpaceOverViewport(id, viewport, flags);
}

void RevealEditorWindow(const char* windowName) {
    const auto* window = ImGui::FindWindowByName(windowName);
    const auto* settings = window ? nullptr : ImGui::FindWindowSettingsByID(ImHashStr(windowName));
    const ImGuiID dockId = window ? window->DockId : (settings ? settings->DockId : 0);
    auto* node = ImGui::DockBuilderGetNode(dockId);
    const auto* root = node ? ImGui::DockNodeGetRootNode(node) : nullptr;
    // KeepAliveOnly のドックはフォーカスだけでは表示されない。現在表示中の配置は維持する。
    if (dockId != 0 && (!root || (root->IsDockSpace() && root->LastFrameActive != ImGui::GetFrameCount()))) {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    }
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowCollapsed(false);
}
} // namespace HIKARI::EDITOR
