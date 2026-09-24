#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {
class DocumentSceneBase;
}
namespace HIKARI::EDITOR {
struct TerrainAuthoringResult {
    EditorOperationResult result;
    std::unique_ptr<IEditorDocumentCommand> command;
    SceneObjectId reveal;
};
using TerrainAuthoringOperation = TerrainAuthoringResult (*)(DocumentSceneBase&, const nlohmann::json&);
TerrainAuthoringResult FillTerrainLayer(DocumentSceneBase& scene, const nlohmann::json& parameters);
TerrainAuthoringResult GenerateTerrain(DocumentSceneBase& scene, const nlohmann::json& parameters);
} // namespace HIKARI::EDITOR
