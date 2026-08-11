#pragma once

#include <cstdint>
#include <string_view>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct SceneComponentData;
    struct SceneDocument;
    struct SceneObjectData;

namespace EDITOR {

    // Read-only discovery and JSON projection for generic component
    // authoring. Mutation orchestration and history remain separate.
    class ComponentAuthoringQueryService {
    public:
        EditorOperationResult QueryObjects(
            const DocumentSceneBase& scene,
            std::string_view exactName,
            std::string_view componentType) const;
        EditorOperationResult QueryComponents(
            const DocumentSceneBase& scene,
            uint64_t objectId) const;

        nlohmann::json SerializeComponent(
            const SceneComponentData& component) const;
        nlohmann::json SerializeObject(
            const SceneDocument& document,
            const SceneObjectData& object) const;
    };

} // namespace EDITOR
} // namespace HIKARI
