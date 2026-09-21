#pragma once
#include <functional>
#include "Editor/Diagnostics/HIKARI_EditorLogPanel.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"

namespace HIKARI::EDITOR {
    struct EditorShellContents {
        std::function<void()> resources, assistant, settings, tasks;
        std::function<void()> documents;
        std::function<void(const char*)> projectWorkspaces;
        bool assistantBusy = false;
        bool tasksBusy = false;
    };
    class EditorShell {
    public:
        void Draw(EditorContext&, EditorWorkspaceHost&, EditorCommandRouter&, const EditorShellContents&);
    private:
        enum class Page { None, Workspaces, Resources, Assistant, Tasks, Log, Settings };
        Page page_ = Page::None;
        bool lastAssistant_ = false, lastSettings_ = false, lastResources_ = false;
        bool initialized_ = false, resizing_ = false;
        bool detachedLog_ = false;
        float width_ = 340;
        uint64_t readErrors_ = 0;
        char workspaceFilter_[128]{};
        EditorLogPanel log_;
    };
}
