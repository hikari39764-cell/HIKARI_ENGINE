#pragma once

#include <functional>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class DocumentSceneBase;
    class IModelMaterialSlotProvider;

namespace EDITOR {

    class EditorOperationRegistry;

    struct ComponentAuthoringOperationContext {
        std::function<DocumentSceneBase*()> sceneProvider{};
        std::function<bool(const DocumentSceneBase&)> dirtyProvider{};
        std::function<void(
            DocumentSceneBase&,
            SceneObjectAuthoringHistoryRequest)> commitHistory{};
        std::function<void(
            DocumentSceneBase&,
            SceneObjectId)> selectObject{};
        IModelMaterialSlotProvider* modelMaterialSlotProvider = nullptr;
    };

    void RegisterComponentAuthoringOperations(
        EditorOperationRegistry& registry,
        ComponentAuthoringOperationContext context);

} // namespace EDITOR
} // namespace HIKARI
