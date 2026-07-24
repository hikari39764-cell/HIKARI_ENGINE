#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"

namespace HIKARI::EDITOR {

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneObjectsHistoryCommand(
            std::string label,
            std::vector<SceneObjectData> beforeObjects,
            std::vector<SceneObjectData> afterObjects,
            SceneCameraSettings beforeCamera,
            SceneCameraSettings afterCamera,
            bool runtimeWorldAffected = true);

} // namespace HIKARI::EDITOR
