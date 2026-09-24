#pragma once

#include <memory>
#include <string>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"

namespace HIKARI::EDITOR {

std::unique_ptr<IEditorDocumentCommand> MakeEnvironmentHistoryCommand(std::string label, SceneEnvironment before,
                                                                      SceneEnvironment after);

} // namespace HIKARI::EDITOR
