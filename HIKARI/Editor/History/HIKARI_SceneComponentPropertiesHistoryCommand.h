#pragma once

#include <memory>
#include <string>

#include <json.hpp>

#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Editor/History/HIKARI_TerrainSurfaceEditDelta.h"

namespace HIKARI::EDITOR {

    // SceneComponentData::properties だけを保持する汎用履歴 command。
    // 大きい SceneDocument 全体を複製せず、object/component の安定 ID で
    // Undo/Redo 対象を再解決する。
    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneComponentPropertiesHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforeProperties,
            nlohmann::json afterProperties,
            EditorDocumentImpact impact =
                EditorDocumentImpact::RuntimeWorld);

    // 巨大な component properties（terrain chunk 等）を複製せず、指定した
    // top-level authoring key だけを Undo/Redo する軽量 patch command。
    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneComponentPropertiesPatchHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforePatch,
            nlohmann::json afterPatch,
            EditorDocumentImpact impact =
                EditorDocumentImpact::RuntimeWorld);

    // Terrain stroke 専用の chunk-key delta。properties["chunks"] 全体を
    // 履歴へ複製せず、触れた chunk の追加/更新/削除と layer mapping だけを
    // 安定 key で適用する。
    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforeDelta,
            nlohmann::json afterDelta,
            EditorDocumentImpact impact =
                EditorDocumentImpact::RuntimeWorld);

} // namespace HIKARI::EDITOR
