#pragma once

#include <memory>
#include <string>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"

namespace HIKARI::EDITOR {

    std::unique_ptr<IEditorDocumentCommand> MakeCinematicsHistoryCommand(
        std::string label,
        SceneCinematicsSettings before,
        SceneCinematicsSettings after);

} // namespace HIKARI::EDITOR
