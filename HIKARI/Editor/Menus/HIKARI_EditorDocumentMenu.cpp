#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"

#include "Core/Text/HIKARI_Localization.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void DrawEditorDocumentMenu(EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::BeginMenu(TEXT::Localize("menu.edit").c_str())) {
        return;
    }

    const EditorCommandBinding* undo = commandRouter.Find(EditorCommandId::Undo);
    const EditorCommandBinding* redo = commandRouter.Find(EditorCommandId::Redo);
    if (ImGui::MenuItem(undo ? undo->label.c_str() : TEXT::Localize("menu.edit.undo").c_str(),
                        EditorCommandRouter::Shortcut(EditorCommandId::Undo), false,
                        commandRouter.CanExecute(EditorCommandId::Undo))) {
        (void)commandRouter.Execute(EditorCommandId::Undo);
    }
    if (ImGui::MenuItem(redo ? redo->label.c_str() : TEXT::Localize("menu.edit.redo").c_str(),
                        EditorCommandRouter::Shortcut(EditorCommandId::Redo), false,
                        commandRouter.CanExecute(EditorCommandId::Redo))) {
        (void)commandRouter.Execute(EditorCommandId::Redo);
    }
    ImGui::Separator();
    const EditorCommandBinding* save = commandRouter.Find(EditorCommandId::SaveDocument);
    if (ImGui::MenuItem(save ? save->label.c_str() : TEXT::Localize("menu.edit.save").c_str(),
                        EditorCommandRouter::Shortcut(EditorCommandId::SaveDocument), false,
                        commandRouter.CanExecute(EditorCommandId::SaveDocument))) {
        (void)commandRouter.Execute(EditorCommandId::SaveDocument);
    }
    ImGui::EndMenu();
#else
    (void)commandRouter;
#endif
}

} // namespace HIKARI::EDITOR
