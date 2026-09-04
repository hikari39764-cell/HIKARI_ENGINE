#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "HIKARI/Project/EditorWorkspace.h"

namespace HIKARI::EDITOR_PROJECT {

    class IGameProjectEditorModule {
    public:
        virtual ~IGameProjectEditorModule() = default;

        virtual std::string_view GetModuleId() const = 0;
        virtual std::vector<std::unique_ptr<IEditorWorkspace>>
            CreateEditorWorkspaces() const = 0;
    };

    // Defined by an optional project Editor companion. The SDK adapter owns
    // the returned module and every workspace until the host unloads the DLL.
    std::unique_ptr<IGameProjectEditorModule>
        CreateGameProjectEditorModule();

} // namespace HIKARI::EDITOR_PROJECT
