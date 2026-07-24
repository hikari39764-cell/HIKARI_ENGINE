#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void DrawEditorDocumentMenu(EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginMenu("Edit")) {
            return;
        }

        const EditorCommandBinding* undo = commandRouter.Find(
            EditorCommandId::Undo);
        const EditorCommandBinding* redo = commandRouter.Find(
            EditorCommandId::Redo);
        if (ImGui::MenuItem(
                undo ? undo->label.c_str() : "Undo",
                EditorCommandRouter::Shortcut(EditorCommandId::Undo),
                false,
                commandRouter.CanExecute(EditorCommandId::Undo))) {
            (void)commandRouter.Execute(EditorCommandId::Undo);
        }
        if (ImGui::MenuItem(
                redo ? redo->label.c_str() : "Redo",
                EditorCommandRouter::Shortcut(EditorCommandId::Redo),
                false,
                commandRouter.CanExecute(EditorCommandId::Redo))) {
            (void)commandRouter.Execute(EditorCommandId::Redo);
        }
        ImGui::Separator();
        const EditorCommandBinding* save = commandRouter.Find(
            EditorCommandId::SaveDocument);
        if (ImGui::MenuItem(
                save ? save->label.c_str() : "Save",
                EditorCommandRouter::Shortcut(
                    EditorCommandId::SaveDocument),
                false,
                commandRouter.CanExecute(
                    EditorCommandId::SaveDocument))) {
            (void)commandRouter.Execute(EditorCommandId::SaveDocument);
        }
        ImGui::EndMenu();
#else
        (void)commandRouter;
#endif
    }

} // namespace HIKARI::EDITOR
