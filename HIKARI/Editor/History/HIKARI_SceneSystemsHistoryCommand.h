#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"

namespace HIKARI::EDITOR {

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneSystemsHistoryCommand(
            std::string label,
            std::vector<SceneSystemData> before,
            std::vector<SceneSystemData> after);

} // namespace HIKARI::EDITOR
