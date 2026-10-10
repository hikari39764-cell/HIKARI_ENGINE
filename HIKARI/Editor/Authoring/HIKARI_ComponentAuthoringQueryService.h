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
    EditorOperationResult QueryTypes(DocumentSceneBase& scene, std::string_view keyword, int offset, int limit) const;
    EditorOperationResult DescribeType(DocumentSceneBase& scene, std::string_view componentType) const;
    EditorOperationResult QueryObjects(const DocumentSceneBase& scene, const nlohmann::json& parameters) const;
    EditorOperationResult QueryComponents(const DocumentSceneBase& scene, uint64_t objectId) const;

    nlohmann::json SerializeComponent(const SceneComponentData& component) const;
    nlohmann::json SerializeObject(const SceneDocument& document, const SceneObjectData& object) const;
    // 一覧と作成の受領書の要約：ID・名前・親・部品・世界の位置と外接箱。
    nlohmann::json SummarizeObject(const DocumentSceneBase& scene, const SceneObjectData& object) const;
};

} // namespace EDITOR
} // namespace HIKARI
