#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "HIKARI/Project/EditorModuleUi.h"

struct ImDrawList;

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

class GameProjectEditorModuleLibrary;

// モジュールが登録した視口工具と窓。工具と窓の実体はモジュールの DLL が持ち、ここは一覧と有効・開閉の状態だけを持つ。
class ModuleEditorExtensions {
public:
    struct Tool {
        std::string moduleId{};
        std::string id{};
        std::string name{};
        EDITOR_PROJECT::IEditorViewportTool* tool = nullptr;
    };
    struct Panel {
        std::string moduleId{};
        std::string id{};
        std::string name{};
        EDITOR_PROJECT::IEditorPanel* panel = nullptr;
        bool open = false;
    };

    void Add(const GameProjectEditorModuleLibrary& module);
    void Clear() noexcept;

    const std::vector<Tool>& GetTools() const noexcept { return tools_; }
    const std::vector<Panel>& GetPanels() const noexcept { return panels_; }
    const Tool* GetActiveTool() const noexcept;
    // 空の id で工具を外す。知らない id は false。
    bool ActivateTool(std::string_view toolId);
    bool SetPanelOpen(std::string_view panelId, bool open);

    // 有効な工具があれば今のフレームの入力を渡す。戻り値はマウスを取ったか（工具が有効なら常に真）。
    bool UpdateViewport(const Camera3D& camera, const SceneViewportRect& viewport, bool hovered, float deltaSeconds,
                        ImDrawList& drawList);
    void DrawPanels(EDITOR_PROJECT::IEditorWorkspaceUi& ui);
    void DrawMenu();

private:
    std::vector<Tool> tools_{};
    std::vector<Panel> panels_{};
    size_t activeTool_ = static_cast<size_t>(-1);
};

} // namespace HIKARI::EDITOR
