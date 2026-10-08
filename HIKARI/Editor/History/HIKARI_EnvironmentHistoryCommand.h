#pragma once

#include <memory>
#include <string>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"

namespace HIKARI::EDITOR {

// シーン文書の環境の参照（プロファイル・天気・初期時刻・地点）の編集。
std::unique_ptr<IEditorDocumentCommand> MakeEnvironmentHistoryCommand(std::string label,
                                                                      ENVIRONMENT::SceneEnvironmentBinding before,
                                                                      ENVIRONMENT::SceneEnvironmentBinding after);

} // namespace HIKARI::EDITOR
