#pragma once

#include <memory>
#include <string>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

// シーン文書の環境の参照（プロファイル・天気・初期時刻・地点）の編集。
std::unique_ptr<IEditorDocumentCommand> MakeEnvironmentHistoryCommand(std::string label,
                                                                      ENVIRONMENT::SceneEnvironmentBinding before,
                                                                      ENVIRONMENT::SceneEnvironmentBinding after);

// シーン文書の光照の焼き込みの質（標本数、反射回数）の編集。
std::unique_ptr<IEditorDocumentCommand> MakeLightingBakeSettingsHistoryCommand(std::string label,
                                                                               SceneLightingBakeSettings before,
                                                                               SceneLightingBakeSettings after);

// シーン文書の世界分区（分けるか、格子の大きさ）の編集。世界には影響せず、保存の形だけが変わる。
std::unique_ptr<IEditorDocumentCommand> MakePartitionSettingsHistoryCommand(std::string label,
                                                                            ScenePartitionSettings before,
                                                                            ScenePartitionSettings after);

} // namespace HIKARI::EDITOR
