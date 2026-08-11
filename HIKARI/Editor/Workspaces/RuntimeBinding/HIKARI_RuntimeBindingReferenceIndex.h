#pragma once

#include <string_view>
#include <vector>

#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    std::vector<RuntimeBindingReference>
        CollectRuntimeBindingReferences(
            const DocumentSceneBase& scene,
            std::string_view bindingId);

} // namespace HIKARI::EDITOR
