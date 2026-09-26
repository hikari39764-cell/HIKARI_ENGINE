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
// 地形の範囲・表面プリセット・現在のブラシと、指定点の高さを返す。
TerrainAuthoringResult DescribeTerrain(DocumentSceneBase& scene, const nlohmann::json& parameters);
// ワールド座標の経路か矩形範囲にブラシを連続で当て、1 回の Undo 差分として記録する。
TerrainAuthoringResult StrokeTerrain(DocumentSceneBase& scene, const nlohmann::json& parameters);
} // namespace HIKARI::EDITOR
