#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"

#include <utility>

#include "Core/Text/HIKARI_Localization.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
void DrawCommandItem(EditorCommandRouter& commandRouter, EditorCommandId command, const char* fallbackKey) {
    const EditorCommandBinding* binding = commandRouter.Find(command);
    if (ImGui::MenuItem(binding ? binding->label.c_str() : TEXT::Localize(fallbackKey).c_str(),
                        EditorCommandRouter::Shortcut(command), false, commandRouter.CanExecute(command))) {
        (void)commandRouter.Execute(command);
    }
}
} // namespace

void DrawEditorDocumentMenu(EditorCommandRouter& commandRouter) {
    if (!ImGui::BeginMenu(TEXT::Localize("menu.edit").c_str())) {
        return;
    }
    DrawCommandItem(commandRouter, EditorCommandId::Undo, "menu.edit.undo");
    DrawCommandItem(commandRouter, EditorCommandId::Redo, "menu.edit.redo");
    // 選んだ物への命令は、今の工作区が持つ時だけ出す（キーだけでは見つけにくいので）。
    bool separated = false;
    for (const EditorCommandId command : {EditorCommandId::DuplicateSelection, EditorCommandId::DeleteSelection,
                                          EditorCommandId::FocusSelection, EditorCommandId::DropSelectionToGround}) {
        if (const EditorCommandBinding* binding = commandRouter.Find(command); binding != nullptr) {
            if (!std::exchange(separated, true)) {
                ImGui::Separator();
            }
            if (ImGui::MenuItem(binding->label.c_str(), EditorCommandRouter::Shortcut(command), false,
                                commandRouter.CanExecute(command))) {
                (void)commandRouter.Execute(command);
            }
        }
    }
    ImGui::Separator();
    DrawCommandItem(commandRouter, EditorCommandId::SaveDocument, "menu.edit.save");
    ImGui::EndMenu();
}

} // namespace HIKARI::EDITOR
