#pragma once
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "Scene/HIKARI_SceneDocument.h"
#include <json.hpp>

namespace HIKARI {
class DocumentSceneBase;
namespace EDITOR {
// 現在の文書と実行時オブジェクトを結び、推測した形状は返さない。
Bounds ObserveSceneObjectBounds(const DocumentSceneBase& scene, SceneObjectId objectId);
nlohmann::json ObserveSceneObject(const DocumentSceneBase& scene, const SceneObjectData& object);
} // namespace EDITOR
} // namespace HIKARI
